#include "pc/stream-bridge-projected-alpha-producer.h"
#include "pc/stream-bridge-projected-alpha-consumer.h"
#include "pc/stream-bridge-projected-alpha-textures.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace stream_bridge::sender;
using namespace stream_bridge::sender::projected_alpha;
namespace {
unsigned checks=0,nativeDrawCalls=0;Producer* active=nullptr;MutexObserver* activeLocks=nullptr;
struct CrossProcess {ChannelLaunch launch{};std::uint64_t source=0,native=0;std::uint32_t compositorPid=0;};
void check(bool okay,const char* why){++checks;if(!okay)throw std::runtime_error(why);}
void releasing(const MutexLease& lease,std::uint64_t key,void*)noexcept{if(active)active->releasing(lease,key);}
void released(const MutexLease& lease,HRESULT result,void*)noexcept{if(active)active->released(lease,result);}
void depthObserved(ID3D11Device*,ID3D11DepthStencilState* state,void*)noexcept{if(active)active->depthState(state);}
void textureObserved(ID3D11Device* device,ID3D11Texture2D* texture,TextureOrigin,void*)noexcept{if(activeLocks)activeLocks->observeTexture(device,texture);}
void targetObserved(ID3D11Device*,ID3D11RenderTargetView* view,void*)noexcept{if(active)active->target(view);}
void bufferObserved(ID3D11Device*,const D3D11_BUFFER_DESC* desc,const D3D11_SUBRESOURCE_DATA* data,ID3D11Buffer* buffer,void*)noexcept{
    if(active)active->geometry().buffer(desc,data,buffer);
}
void layoutObserved(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC* desc,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout* layout,void*)noexcept{
    if(active)active->geometry().layout(desc,count,code,bytes,layout);
}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* context,UINT count,UINT start){++nativeDrawCalls;context->Draw(count,start);}
// Owned DXGI-only forwarding control: a distinct public method table, no
// reverse D3D texture QI, and nested forwarding into the real WARP mutex.
// All keys/results/GPU synchronization still come from the original resource.
class DxgiOnlyMutex final:public IDXGIKeyedMutex {
    std::atomic<ULONG> count_{1};Reference<IDXGIKeyedMutex> native_;
public:
    explicit DxgiOnlyMutex(IDXGIKeyedMutex* native){native->AddRef();native_.value=native;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid==IID_IUnknown||iid==__uuidof(IDXGIObject)||iid==__uuidof(IDXGIDeviceSubObject)||iid==__uuidof(IDXGIKeyedMutex)){
            *out=static_cast<IDXGIKeyedMutex*>(this);AddRef();return S_OK;}
        if(iid==__uuidof(ID3D11Texture2D))return E_NOINTERFACE;
        return native_->QueryInterface(iid,out);
    }
    ULONG STDMETHODCALLTYPE AddRef()override{return count_.fetch_add(1)+1;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=count_.fetch_sub(1)-1;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID id,UINT bytes,const void* data)override{return native_->SetPrivateData(id,bytes,data);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID id,const IUnknown* value)override{return native_->SetPrivateDataInterface(id,value);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID id,UINT* bytes,void* data)override{return native_->GetPrivateData(id,bytes,data);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID iid,void** out)override{return native_->GetParent(iid,out);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID iid,void** out)override{return native_->GetDevice(iid,out);}
    HRESULT STDMETHODCALLTYPE AcquireSync(UINT64 key,DWORD timeout)override{return native_->AcquireSync(key,timeout);}
    HRESULT STDMETHODCALLTYPE ReleaseSync(UINT64 key)override{return native_->ReleaseSync(key);}
};
__declspec(noinline) HRESULT nativeAcquire(IDXGIKeyedMutex* mutex,UINT64 key,DWORD timeout){return mutex->AcquireSync(key,timeout);}
__declspec(noinline) HRESULT nativeRelease(IDXGIKeyedMutex* mutex,UINT64 key){return mutex->ReleaseSync(key);}
constexpr char ProducerFixture[]=R"(
cbuffer Scene : register(b0) {float4 words[4];};
Texture2D<float4> scene : register(t0);
Texture2D<float4> second : register(t1);
Texture2D<float4> third : register(t2);
RWTexture2D<uint> scratch : register(u0);
RWByteAddressBuffer positions : register(u1);
[numthreads(1,1,1)] void compute(uint3 id:SV_DispatchThreadID){scratch[id.xy]=123;}
[numthreads(1,1,1)] void outside(){
    positions.Store4(0,asuint(float4(4,4,7,7)));positions.Store4(16,asuint(float4(4,5,7,8)));
    positions.Store4(32,asuint(float4(5,4,8,7)));positions.Store4(48,asuint(float4(5,5,8,8)));
}
float4 vs(float2 position:POSITION,float2 uv:TEXCOORD0):SV_Position{return float4(2*position-1,0,1);}
float4 reprojectVS(float2 position:POSITION,float2 uv:TEXCOORD0):SV_Position{return float4(2*position-1,0.5,1);}
float4 layoutVS(float3 position:POSITION,float2 uv:TEXCOORD0):SV_Position{return float4(2*position.xy-1,position.z,1);}
float4 ps(float4 xy:SV_Position):SV_Target{
    float4 value=scene.Load(int3(int2(xy.xy),0));
    value.rgb*=words[1].xyz;
    value.a=words[3].y==0?1:value.a;
    return value;
}
float4 copyFp16(float4 xy:SV_Position):SV_Target{return scene.Load(int3(int2(xy.xy),0));}
float4 reprojectCopy(float4 xy:SV_Position):SV_Target{
    int3 p=int3(int2(xy.xy),0);return (scene.Load(p)+second.Load(p)+third.Load(p))/3;
}
float4 boundedY(float4 xy:SV_Position):SV_Target{
    int2 p=int2(xy.xy);p.y=clamp(p.y,0,int(words[0].y)-1);
    float4 value=scene.Load(int3(p,0));value.rgb*=words[1].xyz;
    value.a=words[3].y==0?1:value.a;return value;
}
)";
// Entirely owned semantic controls, not redistributed native shader bodies.
// The resolve deliberately includes alpha-dependent RGB, which the earlier
// opaque-alpha-only resolve fixture omitted.
constexpr char CaptureFixture[]=R"(
cbuffer Scene : register(b0) {float4 words[4];};
cbuffer Policy : register(b2) {float4 flags[2];};
Texture2D<float4> scene : register(t0);
Texture2DMS<float4,4> nativeSamples : register(t0);
Texture2DMS<float4,4> privateSamples : register(t1);
struct Pair {float4 native : SV_Target0;float4 coverage : SV_Target1;};
float4 sceneValue(float4 xy){float4 v=scene.Load(int3(int2(xy.xy),0));v.rgb*=words[1].xyz;return v;}
float4 sceneNative(float4 xy:SV_Position):SV_Target{
    float4 v=sceneValue(xy);if(words[3].y==0)v.a=1;return v;
}
float4 sceneReference(float4 xy:SV_Position):SV_Target{return sceneValue(xy);}
Pair sceneCapture(float4 xy:SV_Position){
    float4 v=sceneValue(xy);Pair p;p.coverage=float4(0,0,0,v.a);
    if(words[3].y==0)v.a=1;p.native=v;return p;
}
float4 nativeAverage(float4 xy){
    int2 p=int2(xy.xy);return ((nativeSamples.Load(p,0)+nativeSamples.Load(p,1))+
        (nativeSamples.Load(p,2)+nativeSamples.Load(p,3)))*0.25;
}
float4 resolveValue(float4 xy){
    float4 v=nativeAverage(xy);
    if(flags[0].x>0){v=saturate(v);v.rgb+=flags[1].xyz*(1-v.a);v.a=1;}
    return v;
}
float4 resolveNative(float4 xy:SV_Position):SV_Target{return resolveValue(xy);}
float4 resolveReference(float4 xy:SV_Position):SV_Target{return nativeAverage(xy);}
Pair resolveCapture(float4 xy:SV_Position){
    Pair p;p.native=resolveValue(xy);int2 at=int2(xy.xy);
    float a=((privateSamples.Load(at,0).a+privateSamples.Load(at,1).a)+
        (privateSamples.Load(at,2).a+privateSamples.Load(at,3).a))*0.25;
    p.coverage=float4(0,0,0,a);return p;
}
)";
Reference<ID3DBlob> compileCapture(const char* entry){
    Reference<ID3DBlob> code,error;
    const auto result=D3DCompile(CaptureFixture,sizeof CaptureFixture-1,nullptr,nullptr,nullptr,
        entry,"ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),error.out());
    if(error.value)std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);
    check(SUCCEEDED(result),"owned native RGBA plus same-draw alpha shader");return code;
}
Reference<ID3DBlob> compile(const char* entry,const char* target){
    Reference<ID3DBlob> code,error;const auto result=D3DCompile(ProducerFixture,sizeof ProducerFixture-1,nullptr,nullptr,nullptr,
        entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),error.out());
    if(error.value)std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);
    check(SUCCEEDED(result),"owned producer shader compilation");return code;
}
std::vector<std::uint8_t> pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* image){
    D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Reference<ID3D11Texture2D> read;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,read.out())),"owned readback allocation");
    context->CopyResource(read.value,image);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(read.value,0,D3D11_MAP_READ,0,&mapped)),"owned pixels map");
    std::vector<std::uint8_t> output(desc.Width*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(output.data()+y*desc.Width*4,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,desc.Width*4);
    context->Unmap(read.value,0);return output;
}
std::uint64_t handle(ID3D11Texture2D* image){Reference<IDXGIResource> resource;HANDLE result=nullptr;
    check(SUCCEEDED(image->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(resource.out())))&&SUCCEEDED(resource->GetSharedHandle(&result))&&result,"owned shared allocation identity");
    return std::uint64_t(reinterpret_cast<std::uintptr_t>(result));
}
void run(const CrossProcess* incoming=nullptr,ChannelPage* external=nullptr,DXGI_FORMAT intermediateFormat=DXGI_FORMAT_UNKNOWN,bool pressure=false,bool padded=false,bool depthTests=false,bool reproject=false,bool dxgiOnly=false){
    const bool filtering=intermediateFormat!=DXGI_FORMAT_UNKNOWN;
    auto vsCode=compile("vs","vs_5_0"),psCode=compile(padded?"boundedY":"ps","ps_5_0"),copyCode=compile(reproject?"reprojectCopy":"copyFp16","ps_5_0");
    Reference<ID3D11ShaderReflection> reflection;D3D11_SHADER_DESC signature{};
    check(SUCCEEDED(D3DReflect(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(reflection.out())))&&SUCCEEDED(reflection->GetDesc(&signature))&&signature.InputParameters==2,
        "actual owned vertex shader retains the native two-attribute input signature");
    auto reprojectCode=compile("reprojectVS","vs_5_0");
    std::array<ShaderFingerprint,4> contracts{};contracts[0].kind=NativeShader::StreamVertex;
    contracts[1].kind=filtering?(reproject?NativeShader::MultiViewReprojectY:NativeShader::MultiViewDownscaleY):NativeShader::MultiViewInner;
    contracts[2].kind=reproject?NativeShader::MultiViewReprojectX:NativeShader::MultiViewDownscaleX;
    contracts[3].kind=NativeShader::MultiViewVertexX;
    check(dxbcFingerprint(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),contracts[0].hash)&&
        dxbcFingerprint(psCode->GetBufferPointer(),psCode->GetBufferSize(),contracts[1].hash)&&
        dxbcFingerprint(copyCode->GetBufferPointer(),copyCode->GetBufferSize(),contracts[2].hash)&&
        dxbcFingerprint(reprojectCode->GetBufferPointer(),reprojectCode->GetBufferSize(),contracts[3].hash),"owned shader contracts");
    NativeShaders shaders(contracts.data(),contracts.size(),nullptr,nullptr,true);
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,
        D3D11_SDK_VERSION,device.out(),nullptr,context.out()))&&shaders.observeDevice(device.value),"owned actual native-style device");
    MutexObserver locks({nullptr,releasing,released,nullptr});activeLocks=&locks;check(locks.prepare(device.value),"original native synchronization observed");
    ChannelParent channel;check(channel.create(),"private producer channel");
    auto* page=external?external:channel.page();
    const unsigned policyBytes=64;Producer producer(&contracts[1],&policyBytes,1,padded?&contracts[1].hash:nullptr);active=&producer;
    check(producer.prepare(device.value,page,shaders,locks),"production controller prepares embedded own kernels");
    TextureObserver textures(textureObserved,nullptr,targetObserved,bufferObserved,layoutObserved,depthObserved);
    check(textures.prepare(device.value),"real public creation callbacks connect native targets and geometry to the producer");
    constexpr UINT width=40,height=36;const std::uint32_t scenePid=77,compositorPid=incoming?incoming->compositorPid:88;
    const UINT intermediateWidth=pressure?1216:width,intermediateHeight=pressure?4608:padded?64:height;
    const std::uint64_t lease=(std::uint64_t(7)<<32)|scenePid;channel.sourceLease(lease);
    std::vector<std::uint8_t> input(width*height*4);
    for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17)%256;input[i*4+1]=UINT(i*39)%256;input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;D3D11_SUBRESOURCE_DATA initial{input.data(),width*4,0};
    Reference<ID3D11Texture2D> source,native;Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11RenderTargetView> target;
    const auto open=[&](std::uint64_t value,ID3D11Texture2D** output){return device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(value)),__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(output));};
    check((incoming?(SUCCEEDED(open(incoming->source,source.out()))&&SUCCEEDED(open(incoming->native,native.out()))):
        (SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out()))))&&
        SUCCEEDED(device->CreateShaderResourceView(source.value,nullptr,sourceView.out()))&&SUCCEEDED(device->CreateRenderTargetView(native.value,nullptr,target.out())),"actual owned source and native target");
    const auto sourceHandle=handle(source.value),nativeHandle=handle(native.value);
    MaskRegistration prepared{};check(page->registrations[0].read(prepared)&&prepared.maskHandle&&producer.stage()==2,"cold preparation published the actual companion allocation");
    Registry registry{};registry.sourceLease=lease;registry.revision=1;
    registry.retained[0]={sourceHandle,21,scenePid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    registry.retained[1]={nativeHandle,22,compositorPid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=source.value;pins[1]=native.value;
    check(external||channel.publishRetained(registry,pins),"real held source registry publication");
    // Actual live GPU targets occupy 240 MiB of ordinary companion storage.
    // The native-like 1216x4608 FP16 image then exceeds the old shared budget.
    std::array<Reference<ID3D11Texture2D>,15> otherTargets;
    std::array<Reference<ID3D11RenderTargetView>,15> otherViews;
    Reference<ID3D11Texture2D> refusedOrdinary;Reference<ID3D11RenderTargetView> refusedOrdinaryView;
    if(pressure){
        auto large=desc;large.Width=large.Height=2048;large.MiscFlags=0;
        for(unsigned i=0;i<otherTargets.size();++i)
            check(SUCCEEDED(device->CreateTexture2D(&large,nullptr,otherTargets[i].out()))&&
                SUCCEEDED(device->CreateRenderTargetView(otherTargets[i].value,nullptr,otherViews[i].out())),"actual live ordinary GPU target pressure");
        const auto before=producer.diagnostics().rgbaBytes;
        const auto floatingBytes=std::uint64_t(intermediateWidth)*intermediateHeight*8;
        check(before==240ull*1024*1024+width*height*4&&before+floatingBytes>CompanionClassBudget,
            "actual admitted ordinary targets would reject the required FP16 image under the old common budget");
        large.Height=3072;
        check(SUCCEEDED(device->CreateTexture2D(&large,nullptr,refusedOrdinary.out()))&&
            SUCCEEDED(device->CreateRenderTargetView(refusedOrdinary.value,nullptr,refusedOrdinaryView.out()))&&
            producer.diagnostics().rgbaBytes==before,"ordinary targets still cannot consume their reserved FP16 allowance");
    }
    Reference<ID3D11VertexShader> vs;Reference<ID3D11PixelShader> ps,copy;Reference<ID3D11InputLayout> layout;Reference<ID3D11Buffer> vertices,constants;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out()))&&
        SUCCEEDED(device->CreatePixelShader(copyCode->GetBufferPointer(),copyCode->GetBufferSize(),nullptr,copy.out())),"actual observed native-model program objects");
    check(shaders.identify(vs.value)==NativeShader::StreamVertex&&shaders.identify(ps.value)==contracts[1].kind&&
        shaders.identify(copy.value)==contracts[2].kind,"actual shader observations survive cold companion preparation");
    Reference<ID3D11Texture2D> intermediate;Reference<ID3D11RenderTargetView> intermediateTarget,integerTarget;
    Reference<ID3D11ShaderResourceView> intermediateView;
    if(filtering){
        auto floating=desc;floating.Format=intermediateFormat;floating.MiscFlags=0;floating.Width=intermediateWidth;floating.Height=intermediateHeight;
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};rtv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rtv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        check(SUCCEEDED(device->CreateTexture2D(&floating,nullptr,intermediate.out()))&&
            SUCCEEDED(device->CreateShaderResourceView(intermediate.value,&srv,intermediateView.out())),"actual native-like FP16 storage and typed FLOAT views");
        if(intermediateFormat==DXGI_FORMAT_R16G16B16A16_TYPELESS){
            rtv.Format=DXGI_FORMAT_R16G16B16A16_UINT;
            check(SUCCEEDED(device->CreateRenderTargetView(intermediate.value,&rtv,integerTarget.out())),"typeless storage also has a distinct integer interpretation");
            check(producer.diagnostics().fp16Prepared==0&&producer.diagnostics().fp16Refusal==6,
                "actual creation callback refuses the integer interpretation of typeless storage");
        }
        rtv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        check(SUCCEEDED(device->CreateRenderTargetView(intermediate.value,&rtv,intermediateTarget.out())),"actual native FLOAT RTV creation");
        check(producer.diagnostics().fp16Prepared==1,"the real FLOAT RTV creation callback prepared the native intermediate");
        check(producer.diagnostics().fp16Bytes==std::uint64_t(intermediateWidth)*intermediateHeight*8&&
            producer.diagnostics().rgbaBytes<=CompanionClassBudget&&producer.diagnostics().fp16Bytes<=CompanionClassBudget,
            "FP16 preparation owns its exact separate bounded reservation");
    }
    // Keep more than sixteen distinct live native objects before the actual
    // game quad. Metadata must follow each object, not the first sixteen ever.
    std::array<Reference<ID3D11InputLayout>,48> otherLayouts;
    std::array<Reference<ID3D11Buffer>,48> otherBuffers;
    std::array<std::uint8_t,256> otherBytes{};
    for(UINT i=0;i<otherLayouts.size();++i){
        const D3D11_INPUT_ELEMENT_DESC otherElements[]{
            {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,64+4*i,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
        check(SUCCEEDED(device->CreateInputLayout(otherElements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),otherLayouts[i].out())),"real unrelated layout pressure");
        D3D11_BUFFER_DESC otherDesc{};otherDesc.ByteWidth=32+4*i;otherDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA otherData{otherBytes.data(),0,0};
        check(SUCCEEDED(device->CreateBuffer(&otherDesc,&otherData,otherBuffers[i].out())),"real unrelated vertex allocation pressure");
    }
    auto layoutCode=compile("layoutVS","vs_5_0");
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(SUCCEEDED(device->CreateInputLayout(elements,2,layoutCode->GetBufferPointer(),layoutCode->GetBufferSize(),layout.out())),"native-style float3 position plus UV layout from a wider creation signature");
    // Deliberately different UVs must not become position evidence.
    const std::array<float,20> points{{0,0,99,7,7,0,1,99,7,8,1,0,99,8,7,1,1,99,8,8}};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof points;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData{points.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&vertexData,vertices.out())),"actual vertex buffer");
    std::array<float,16> words{};words[1]=float(height);words[4]=words[5]=words[6]=1;buffer.ByteWidth=sizeof words;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData{words.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&constantData,constants.out())),"native alpha policy constant buffer");
    Reference<IDXGIKeyedMutex> sourceMutex,streamMutex;
    check(SUCCEEDED(source->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(sourceMutex.out())))&&
        SUCCEEDED(native->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(streamMutex.out()))),"original application and stream mutexes");
    if(dxgiOnly){
        auto* facade=new DxgiOnlyMutex(sourceMutex.value);Reference<ID3D11Texture2D> unsupported;
        check(facade->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(unsupported.out()))==E_NOINTERFACE,
            "owned actual mutex facade does not expose a D3D texture wrapper");
        const auto before=locks.diagnostics().acquireCalls;
        check(locks.observeMutex(device.value,facade)&&locks.diagnostics().tables>=2&&locks.diagnostics().acquireCalls==before,
            "a distinct actual DXGI interface table is admitted cold without acquiring the native resource");
        sourceMutex->Release();sourceMutex.value=facade;
    }
    const auto coldSync=locks.diagnostics();
    check(coldSync.textures&&coldSync.shared>=2&&coldSync.keyed>=2&&coldSync.tables&&!coldSync.admissionRefused,
        "actual created and imported native textures receive cold synchronization admission");
    const auto callsBefore=coldSync.acquireCalls;
    locks.observeTexture(device.value,source.value);
    check(locks.diagnostics().acquireCalls==callsBefore&&!locks.current(device.value,sourceHandle),"cold native mutex admission performs no acquisition and grants no lease");
    auto plainDesc=desc;plainDesc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;Reference<ID3D11Texture2D> plain;
    check(SUCCEEDED(device->CreateTexture2D(&plainDesc,nullptr,plain.out()))&&locks.diagnostics().nonKeyed,
        "real shared textures without a keyed interface stay diagnostic and gain no ownership");
    check(nativeAcquire(sourceMutex.value,0,0)==S_OK&&nativeAcquire(streamMutex.value,0,0)==S_OK,"actual producer ownership");
    const auto actualSync=locks.diagnostics();check(actualSync.acquireCalls==callsBefore+2&&actualSync.acquireOkay>=2&&actualSync.active==2&&!actualSync.identityRefused,
        "counters observe the two actual native acquisitions and currently held leases");
    context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps.value,nullptr,0);context->PSSetConstantBuffers(0,1,&constants.value);
    context->IASetInputLayout(layout.value);UINT stride=20,offset=0;context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->OMSetRenderTargets(1,&target.value,nullptr);context->PSSetShaderResources(0,1,&sourceView.value);
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    Reference<ID3D11VertexShader> boundVertex;Reference<ID3D11RenderTargetView> boundTarget;
    context->VSGetShader(boundVertex.out(),nullptr,nullptr);context->OMGetRenderTargets(1,boundTarget.out(),nullptr);
    check(boundTarget.value==target.value&&shaders.identify(boundVertex.value)==NativeShader::StreamVertex,"actual native target and vertex program are bound under ownership");
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"production bound vertex range proves full image coverage");
    check(bool(locks.current(device.value,sourceHandle)),"actual source-allocation ownership is retained");
    auto computeCode=compile("compute","cs_5_0"),outsideCode=compile("outside","cs_5_0");
    Reference<ID3D11ComputeShader> compute,outside;
    check(SUCCEEDED(device->CreateComputeShader(computeCode->GetBufferPointer(),computeCode->GetBufferSize(),nullptr,compute.out()))&&
        SUCCEEDED(device->CreateComputeShader(outsideCode->GetBufferPointer(),outsideCode->GetBufferSize(),nullptr,outside.out())),"actual native-style independent compute passes");
    D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=scratchDesc.Height=scratchDesc.MipLevels=scratchDesc.ArraySize=scratchDesc.SampleDesc.Count=1;
    scratchDesc.Format=DXGI_FORMAT_R32_UINT;scratchDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Reference<ID3D11Texture2D> scratchImage;Reference<ID3D11UnorderedAccessView> scratchView;
    check(SUCCEEDED(device->CreateTexture2D(&scratchDesc,nullptr,scratchImage.out()))&&
        SUCCEEDED(device->CreateUnorderedAccessView(scratchImage.value,nullptr,scratchView.out())),"separate writable compute output");
    context->CSSetShader(compute.value,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&scratchView.value,nullptr);
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"unrelated compute cannot erase actual native vertex creation evidence");
    D3D11_BUFFER_DESC mutableDesc{};mutableDesc.ByteWidth=sizeof points;mutableDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_UNORDERED_ACCESS;
    mutableDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    Reference<ID3D11Buffer> mutableVertices;Reference<ID3D11UnorderedAccessView> mutableView;
    check(SUCCEEDED(device->CreateBuffer(&mutableDesc,&vertexData,mutableVertices.out())),"native vertex allocation with explicit raw compute access");
    D3D11_UNORDERED_ACCESS_VIEW_DESC mutableViewDesc{};mutableViewDesc.Format=DXGI_FORMAT_R32_TYPELESS;
    mutableViewDesc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;mutableViewDesc.Buffer.NumElements=sizeof points/4;mutableViewDesc.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    check(SUCCEEDED(device->CreateUnorderedAccessView(mutableVertices.value,&mutableViewDesc,mutableView.out())),"explicit raw UAV for the actual vertex allocation");
    context->IASetVertexBuffers(0,1,&mutableVertices.value,&stride,&offset);
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"known mutable vertex data initially covers the actual target");
    ID3D11Buffer* noBuffer=nullptr;context->IASetVertexBuffers(0,1,&noBuffer,&stride,&offset);
    context->CSSetShader(outside.value,nullptr,0);context->CSSetUnorderedAccessViews(1,1,&mutableView.value,nullptr);
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    ID3D11UnorderedAccessView* noView=nullptr;context->CSSetUnorderedAccessViews(1,1,&noView,nullptr);
    context->IASetVertexBuffers(0,1,&mutableVertices.value,&stride,&offset);
    unsigned reason=0;
    check(!producer.geometry().fullUnitQuad(context.value,4,0,width,height,1,&reason)&&reason==7,"actual bound compute writes revoke that vertex allocation before reuse");
    D3D11_BUFFER_DESC readDesc=mutableDesc;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=readDesc.MiscFlags=readDesc.StructureByteStride=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Reference<ID3D11Buffer> readVertices;check(SUCCEEDED(device->CreateBuffer(&readDesc,nullptr,readVertices.out())),"owned compute-write reference readback");
    context->CopyResource(readVertices.value,mutableVertices.value);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(readVertices.value,0,D3D11_MAP_READ,0,&mapped)),"observe actual changed vertex bytes on the owned GPU target");
    check(static_cast<const float*>(mapped.pData)[0]==4&&static_cast<const float*>(mapped.pData)[1]==4,"compute really moved the vertices outside the image");context->Unmap(readVertices.value,0);
    context->UpdateSubresource(mutableVertices.value,0,nullptr,points.data(),0,0);producer.geometry().update(mutableVertices.value,points.data());
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"fresh public vertex data repairs only its actual allocation");
    context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);context->CSSetShader(compute.value,nullptr,0);
    if(filtering){
        context->PSSetShader(copy.value,nullptr,0);
        if(integerTarget.value){
            context->OMSetRenderTargets(1,&integerTarget.value,nullptr);
            const auto before=producer.diagnostics().pairedDraws;producer.draw(context.value,draw,4,0);
            check(producer.diagnostics().pairedDraws==before,"integer RTV cannot borrow the FLOAT interpretation of the same native storage");
        }
        context->OMSetRenderTargets(1,&intermediateTarget.value,nullptr);
        Reference<ID3D11VertexShader> reprojectionVertex;
        if(reproject){
            check(SUCCEEDED(device->CreateVertexShader(reprojectCode->GetBufferPointer(),reprojectCode->GetBufferSize(),nullptr,reprojectionVertex.out())),"owned separate reprojection vertex contract");
            context->VSSetShader(reprojectionVertex.value,nullptr,0);
            const auto before=producer.diagnostics().pairedDraws;producer.draw(context.value,draw,4,0);
            check(producer.diagnostics().pairedDraws==before,"private cold companion initialization alone cannot authorize a reprojection draw");
            ID3D11ShaderResourceView* same[3]{sourceView.value,sourceView.value,sourceView.value};context->PSSetShaderResources(0,3,same);
        }
        const FLOAT zero[4]{},paddingPoison[4]{0.75f,0.25f,0.5f,1};
        if(reproject){
            Reference<ID3D11DeviceContext1> extended;check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"real public reprojection ClearView");
            producer.clearView(context.value,intermediateTarget.value,zero,nullptr,0);extended->ClearView(intermediateTarget.value,zero,nullptr,0);
        }else{
            if(!padded)producer.clear(context.value,intermediateTarget.value,zero);
            context->ClearRenderTargetView(intermediateTarget.value,padded?paddingPoison:zero);
        }
        // Only the real native viewport is written. The trailing allocation
        // rows deliberately retain opaque, nonzero data and confer no proof.
        const D3D11_VIEWPORT floatingViewport{0,0,float(intermediateWidth),float(padded?height:intermediateHeight),0,1};context->RSSetViewports(1,&floatingViewport);
        producer.draw(context.value,draw,4,0);draw(context.value,4,0);
        check(producer.stage()==16&&producer.diagnostics().rootPasses==(reproject?3u:1u)&&producer.diagnostics().pairedDraws==1,
            "production X pass captures actual source ancestry into an FP16 companion for this exact native target");
        check(producer.diagnostics().fp16ZeroClears==(padded?0u:1u)&&producer.diagnostics().fp16OtherClears==0&&producer.diagnostics().geometryEmpty==(padded?0u:1u),
            "the diagnostic distinguishes an observed native zero clear from private companion initialization");
        ID3D11RenderTargetView* none=nullptr;context->OMSetRenderTargets(1,&none,nullptr);
        context->PSSetShaderResources(0,1,&intermediateView.value);context->OMSetRenderTargets(1,&target.value,nullptr);
        context->RSSetViewports(1,&viewport);
        context->PSSetShader(ps.value,nullptr,0);context->VSSetShader(vs.value,nullptr,0);
    }
    Reference<ID3D11Texture2D> depthImage;Reference<ID3D11DepthStencilView> depthView;Reference<ID3D11DepthStencilState> nativeDepth;
    if(padded){
        const auto before=producer.diagnostics().pairedDraws;
        words[1]=float(intermediateHeight);context->UpdateSubresource(constants.value,0,nullptr,words.data(),0,0);producer.geometry().update(constants.value,words.data());
        producer.draw(context.value,draw,4,0);
        check(producer.stage()==2032&&producer.diagnostics().pairedDraws==before,"a Y clamp extending into unwritten native padding is refused");
        words[1]=float(height);context->UpdateSubresource(constants.value,0,nullptr,words.data(),0,0);
        producer.geometry().invalidate(constants.value);producer.draw(context.value,draw,4,0);
        check(producer.stage()==2031&&producer.diagnostics().pairedDraws==before,"unknown current logical constants cannot borrow prior sample bounds");
        producer.geometry().update(constants.value,words.data());
        Reference<ID3D11DeviceContext1> ranges;check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(ranges.out()))),"public logical constant range");
        std::array<float,128> rangeWords{};std::copy(words.begin(),words.end(),rangeWords.begin()+64);
        D3D11_BUFFER_DESC rangeDesc{};rangeDesc.ByteWidth=sizeof rangeWords;rangeDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA rangeData{rangeWords.data(),0,0};Reference<ID3D11Buffer> rangeBuffer;
        check(SUCCEEDED(device->CreateBuffer(&rangeDesc,&rangeData,rangeBuffer.out())),"owned nonzero logical range buffer");
        UINT first=16,number=16;ranges->PSSetConstantBuffers1(0,1,&rangeBuffer.value,&first,&number);
        std::array<float,4> actual{};check(producer.geometry().constantPrefix(context.value,actual.data(),sizeof actual)&&actual[1]==float(height),"bounds follow the actual nonzero logical range, not allocation byte zero");
        first=32;ranges->PSSetConstantBuffers1(0,1,&rangeBuffer.value,&first,&number);
        check(!producer.geometry().constantPrefix(context.value,actual.data(),sizeof actual),"a logical range outside the observed allocation refuses");
        context->PSSetConstantBuffers(0,1,&constants.value);
        std::printf("owned padded viewport=%ux%u, native storage=%ux%u; poisoned padding and unknown/outside bounds refused\n",width,height,intermediateWidth,intermediateHeight);
    }
    if(depthTests){
        auto depthShape=desc;depthShape.Format=DXGI_FORMAT_R24G8_TYPELESS;depthShape.BindFlags=D3D11_BIND_DEPTH_STENCIL;depthShape.MiscFlags=0;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};dsv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dsv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        D3D11_DEPTH_STENCIL_DESC state{};state.DepthEnable=TRUE;state.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;state.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        state.StencilEnable=TRUE;state.StencilReadMask=state.StencilWriteMask=255;
        state.FrontFace.StencilFunc=D3D11_COMPARISON_EQUAL;
        state.FrontFace.StencilPassOp=D3D11_STENCIL_OP_INCR_SAT;state.FrontFace.StencilFailOp=state.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP;
        state.BackFace=state.FrontFace;
        check(SUCCEEDED(device->CreateTexture2D(&depthShape,nullptr,depthImage.out()))&&SUCCEEDED(device->CreateDepthStencilView(depthImage.value,&dsv,depthView.out()))&&
            SUCCEEDED(device->CreateDepthStencilState(&state,nativeDepth.out())),"owned real native depth/stencil target and comparison state");
        context->ClearDepthStencilView(depthView.value,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,3);
        // Prepare an actual split stencil image on the GPU. Only the left
        // half will pass either native comparison; the right must stay zero
        // in both ordinary RGB and the companion's fractional alpha.
        auto fill=state;fill.DepthEnable=FALSE;fill.DepthFunc=D3D11_COMPARISON_ALWAYS;
        fill.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;fill.FrontFace.StencilPassOp=D3D11_STENCIL_OP_REPLACE;fill.BackFace=fill.FrontFace;
        Reference<ID3D11DepthStencilState> fillState;Reference<ID3D11RasterizerState> scissorState,priorRaster;
        D3D11_RASTERIZER_DESC scissor{};scissor.FillMode=D3D11_FILL_SOLID;scissor.CullMode=D3D11_CULL_NONE;scissor.ScissorEnable=TRUE;scissor.DepthClipEnable=TRUE;
        check(SUCCEEDED(device->CreateDepthStencilState(&fill,fillState.out()))&&SUCCEEDED(device->CreateRasterizerState(&scissor,scissorState.out())),"owned split stencil preparation states");
        context->RSGetState(priorRaster.out());context->RSSetState(scissorState.value);
        const D3D11_RECT leftHalf{0,0,LONG(width/2),LONG(height)};context->RSSetScissorRects(1,&leftHalf);
        context->OMSetRenderTargets(0,nullptr,depthView.value);context->OMSetDepthStencilState(fillState.value,7);draw(context.value,4,0);
        context->RSSetState(priorRaster.value);
        context->OMSetRenderTargets(1,&target.value,depthView.value);context->OMSetDepthStencilState(nativeDepth.value,7);
        const auto before=producer.diagnostics().pairedDraws;producer.draw(context.value,draw,4,0);
        check(producer.stage()==2021&&producer.diagnostics().pairedDraws==before,"conditional depth/stencil draw cannot complete an unknown destination");
        Reference<ID3D11DeviceContext1> extended;check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"real public ClearView context");
        const FLOAT zero[4]{};const D3D11_RECT partial{0,0,LONG(width/2),LONG(height)};
        producer.clearView(context.value,target.value,zero,&partial,1);extended->ClearView(target.value,zero,&partial,1);
        producer.draw(context.value,draw,4,0);check(producer.stage()==2021&&producer.diagnostics().pairedDraws==before,"partial native ClearView is not a full-image zero baseline");
        producer.clearView(context.value,target.value,zero,nullptr,0);extended->ClearView(target.value,zero,nullptr,0);
    }
    producer.draw(context.value,draw,4,0);draw(context.value,4,0);
    if(depthTests){
        context->OMSetDepthStencilState(nativeDepth.value,8);
        producer.draw(context.value,draw,4,0);draw(context.value,4,0);
        Reference<ID3D11DepthStencilState> after;UINT reference=0;context->OMGetDepthStencilState(after.out(),&reference);
        check(after.value==nativeDepth.value&&reference==8,"both companion passes restore the exact original depth/stencil state and reference");
    }
    context->Flush();
    check(producer.stage()==16&&producer.diagnostics().rootPasses==(reproject?3u:1u)&&producer.diagnostics().pairedDraws==(filtering?(depthTests?3u:2u):1u),
        "actual producer completes the owned direct or filtered ancestry route before original release");
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    std::printf("production stage after draw=%u\n",producer.stage());
    check(streamMutex->ReleaseSync(3)==S_OK,"native stream ownership transferred with its unchanged release key");
    if(incoming){
        check(nativeRelease(sourceMutex.value,0)==S_OK,"child source ownership released after paired image");
        BYTE finished=0;DWORD bytes=0;
        check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&finished,1,&bytes,nullptr)&&bytes==1&&finished==1,"parent acknowledges its actual paired copy");
        active=nullptr;activeLocks=nullptr;context->ClearState();return;
    }
    std::printf("production stage after release=%u\n",producer.stage());
    if(pressure){const auto storage=producer.diagnostics();
        std::printf("actual GPU pressure: rgba/fp16 bytes=%llu/%llu; bounded independently; native image sealed\n",
            static_cast<unsigned long long>(storage.rgbaBytes),static_cast<unsigned long long>(storage.fp16Bytes));}
    ImageWitness witness{};check(page->witnesses[0].read(witness)&&witness.complete&&witness.sourceLease==lease&&
        witness.scene.pid==scenePid&&witness.native.pid==compositorPid&&witness.native.generation==22,"production witness separates authenticated game ancestry from actual compositor ownership");
    bool sealed=false;for(const auto& release:witness.releases)sealed|=release.allocation==nativeHandle&&release.key==3&&release.scope>1;
    check(sealed,"witness sealed by the original native output release");
    Reference<ID3D11Texture2D> mask;check(SUCCEEDED(device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(witness.maskHandle)),
        __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(mask.out()))),"import actual production mask");
    check(streamMutex->AcquireSync(3,0)==S_OK,"actual consumer owns the exact image version");
    const auto nativePixels=pixels(device.value,context.value,native.value),maskPixels=pixels(device.value,context.value,mask.value);
    for(UINT i=0;i<width*height;++i){const bool visible=!depthTests||(i%width)<width/2;
        check(nativePixels[i*4+3]==(visible?255:0),"native opaque-alpha policy and actual rejected stencil region remain unchanged");
        check(maskPixels[i*4+3]==(visible?input[i*4+3]:0),"production alpha preserves source coverage only where the native comparison passed");
        for(unsigned c=0;c<3;++c)check(nativePixels[i*4+c]==(visible?input[i*4+c]:0)&&!maskPixels[i*4+c],"native RGB including additive zero-alpha colour unchanged; no RGB exported");}
    if(depthTests){
        const auto actual=pixels(device.value,context.value,depthImage.value);
        for(UINT i=0;i<width*height;++i){const bool visible=(i%width)<width/2;
            check(actual[i*4+3]==(visible?9:3),"original passes increment accepted stencil exactly once and preserve rejected pixels; companion writes none");
            check(visible?(!actual[i*4]&&!actual[i*4+1]&&!actual[i*4+2]):(actual[i*4]==255&&actual[i*4+1]==255&&actual[i*4+2]==255),"actual depth writes belong only to accepted native pixels");}
        std::printf("owned depth/stencil comparison: two exact native updates, no companion attachment writes; native RGB and alpha-only image verified\n");
    }
    producer.invalidate(native.value);check(page->witnesses[0].read(witness)&&!witness.complete,"unsupported native write revokes the previously sealed image before reuse");
    check(streamMutex->ReleaseSync(0)==S_OK&&streamMutex->AcquireSync(0,0)==S_OK,"next exact native ownership interval");
    channel.sourceLease(lease+0x100000000ull);producer.draw(context.value,draw,4,0);draw(context.value,4,0);
    check(streamMutex->ReleaseSync(0)==S_OK&&page->witnesses[0].read(witness)&&!witness.complete,"revoked app session cannot republish old coverage");
    check(nativeRelease(sourceMutex.value,0)==S_OK,"native application ownership ends");
    active=nullptr;activeLocks=nullptr;context->ClearState();
}
void nativeSingleDrawControl(){
    auto vsCode=compile("vs","vs_5_0"),psCode=compile("ps","ps_5_0");
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,
        D3D11_SDK_VERSION,device.out(),nullptr,context.out())),"actual single-draw WARP device");
    Replay operation;check(operation.prepare(device.value,{kernels::Clone64.data(),kernels::Clone64.size()},
        {kernels::Clone80.data(),kernels::Clone80.size()},{kernels::Clone32.data(),kernels::Clone32.size()}),"cold single-draw policy preparation");
    constexpr UINT width=40,height=36;
    std::vector<std::uint8_t> data(width*height*4);
    for(UINT i=0;i<width*height;++i){data[i*4]=UINT(i*17)%256;data[i*4+1]=UINT(i*39)%256;data[i*4+2]=UINT(i*71)%256;data[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
    D3D11_SUBRESOURCE_DATA initial{data.data(),width*4,0};
    Reference<ID3D11Texture2D> source,output;Reference<ID3D11ShaderResourceView> view;Reference<ID3D11RenderTargetView> target;
    check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,output.out()))&&
        SUCCEEDED(device->CreateShaderResourceView(source.value,nullptr,view.out()))&&SUCCEEDED(device->CreateRenderTargetView(output.value,nullptr,target.out())),"plain shared colour resources");
    Reference<IDXGIKeyedMutex> absent;
    check(source->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(absent.out()))==E_NOINTERFACE&&
        output->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(absent.out()))==E_NOINTERFACE,
        "both actual source and output deliberately have no keyed-mutex interface");
    Reference<ID3D11VertexShader> vs;Reference<ID3D11PixelShader> ps;Reference<ID3D11InputLayout> layout;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out())),"unchanged original shaders");
    const D3D11_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(SUCCEEDED(device->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.out())),"actual original layout");
    const std::array<float,16> points{{0,0,7,7,0,1,7,8,1,0,8,7,1,1,8,8}};
    D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof points;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA verticesData{points.data(),0,0};Reference<ID3D11Buffer> vertices;
    check(SUCCEEDED(device->CreateBuffer(&buffer,&verticesData,vertices.out())),"native original vertex bytes");
    context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps.value,nullptr,0);context->PSSetShaderResources(0,1,&view.value);
    context->IASetInputLayout(layout.value);UINT stride=16,offset=0;context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->OMSetRenderTargets(1,&target.value,nullptr);
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    for(unsigned bytes:std::array<unsigned,2>{64,80}){
        std::array<float,20> words{};words[4]=words[5]=words[6]=1;
        buffer={};buffer.ByteWidth=bytes;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA constantsData{words.data(),0,0};Reference<ID3D11Buffer> constants;
        check(SUCCEEDED(device->CreateBuffer(&buffer,&constantsData,constants.out())),"native policy buffer for both reviewed sizes");
        context->PSSetConstantBuffers(0,1,&constants.value);draw(context.value,4,0);
        const auto ordinary=pixels(device.value,context.value,output.value);const auto before=nativeDrawCalls;
        check(operation.nativeAlpha(context.value,ps.value,bytes,false,draw,4,0)&&nativeDrawCalls==before+1,
            "native alpha executes the original scene draw exactly once, without replaying its source");
        const auto alpha=pixels(device.value,context.value,output.value);
        for(UINT i=0;i<width*height;++i){
            check(ordinary[i*4+3]==255&&alpha[i*4+3]==data[i*4+3],"real alpha survives the original opaque-output policy");
            for(unsigned c=0;c<3;++c)check(ordinary[i*4+c]==alpha[i*4+c]&&alpha[i*4+c]==data[i*4+c],"every native RGB byte, including positive emission at alpha zero, remains unchanged");
        }
        Reference<ID3D11Buffer> restored;context->PSGetConstantBuffers(0,1,restored.out());
        Reference<ID3D11PixelShader> restoredProgram;context->PSGetShader(restoredProgram.out(),nullptr,nullptr);
        Reference<ID3D11RenderTargetView> restoredTarget;context->OMGetRenderTargets(1,restoredTarget.out(),nullptr);
        check(restored.value==constants.value&&restoredProgram.value==ps.value&&restoredTarget.value==target.value,
            "original buffer, program and render target remain bound after the single native draw");
        check(!operation.nativeAlpha(context.value,ps.value,48,false,draw,4,0)&&nativeDrawCalls==before+1,"unknown policy prefix neither draws nor grants alpha capture");
    }
    D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;blend.AlphaToCoverageEnable=TRUE;
    Reference<ID3D11BlendState> coverageBlend;check(SUCCEEDED(device->CreateBlendState(&blend,coverageBlend.out())),"actual alpha-dependent sample coverage state");
    context->OMSetBlendState(coverageBlend.value,nullptr,UINT_MAX);const auto before=nativeDrawCalls;
    check(!operation.nativeAlpha(context.value,ps.value,64,false,draw,4,0)&&nativeDrawCalls==before,"alpha-to-coverage cannot silently change RGB sample coverage");
    context->OMSetBlendState(nullptr,nullptr,UINT_MAX);context->ClearState();
    std::printf("owned single native draw: plain-shared non-keyed input/output;64/80-byte policies; exact original RGB and true alpha; no image/frame permit yet\n");
}
void nativeMrtControl(){
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,
        D3D11_SDK_VERSION,device.out(),nullptr,context.out())),"actual MRT single-threaded WARP device");
    Replay operation;check(operation.prepare(device.value,{kernels::Clone64.data(),kernels::Clone64.size()},
        {kernels::Clone80.data(),kernels::Clone80.size()},{kernels::Clone32.data(),kernels::Clone32.size()}),"cold MRT preparation");
    constexpr UINT width=40,height=36;
    auto vsCode=compile("vs","vs_5_0");Reference<ID3D11VertexShader> vs;Reference<ID3D11InputLayout> layout;
    const D3D11_INPUT_ELEMENT_DESC elements[]{{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.out())),"unchanged owned native vertex pipeline");
    const std::array<float,16> points{{0,0,7,7,0,1,7,8,1,0,8,7,1,1,8,8}};
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof points;bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA verticesData{points.data(),0,0};Reference<ID3D11Buffer> vertices;
    check(SUCCEEDED(device->CreateBuffer(&bd,&verticesData,vertices.out())),"original owned quad");
    UINT stride=16,offset=0;context->IASetInputLayout(layout.value);context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->VSSetShader(vs.value,nullptr,0);
    std::array<Reference<ID3D11PixelShader>,6> programs;
    const std::array<const char*,6> names{{"sceneNative","sceneReference","sceneCapture","resolveNative","resolveReference","resolveCapture"}};
    for(unsigned i=0;i<names.size();++i){auto code=compileCapture(names[i]);
        check(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,programs[i].out())),"owned native/reference/augmented pixel programs");}
    auto xCode=compile("copyFp16","ps_5_0");Reference<ID3D11PixelShader> xProgram;
    check(SUCCEEDED(device->CreatePixelShader(xCode->GetBufferPointer(),xCode->GetBufferSize(),nullptr,xProgram.out())),"unchanged FP16 X program");
    struct Target {Reference<ID3D11Texture2D> image;Reference<ID3D11ShaderResourceView> source;Reference<ID3D11RenderTargetView> target;};
    const auto create=[&](Target& target,UINT samples,DXGI_FORMAT format,bool shared){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
        desc.SampleDesc.Count=samples;desc.Format=format;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags=shared?D3D11_RESOURCE_MISC_SHARED:0;
        check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,target.image.out()))&&
            SUCCEEDED(device->CreateShaderResourceView(target.image.value,nullptr,target.source.out()))&&
            SUCCEEDED(device->CreateRenderTargetView(target.image.value,nullptr,target.target.out())),"native chain resource");
    };
    Target source,x,normalMsaa,pairedMsaa,referenceMsaa,naiveMsaa,normalFinal,pairedFinal,referenceFinal,naiveFinal;
    create(source,1,DXGI_FORMAT_R8G8B8A8_UNORM,true);create(x,1,DXGI_FORMAT_R16G16B16A16_FLOAT,false);
    for(auto* target:{&normalMsaa,&pairedMsaa,&referenceMsaa,&naiveMsaa})create(*target,4,DXGI_FORMAT_R8G8B8A8_UNORM,false);
    for(auto* target:{&normalFinal,&pairedFinal,&referenceFinal,&naiveFinal})create(*target,1,DXGI_FORMAT_R8G8B8A8_UNORM,true);
    Reference<IDXGIKeyedMutex> absent;
    check(source.image->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(absent.out()))==E_NOINTERFACE&&
        pairedFinal.image->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(absent.out()))==E_NOINTERFACE,
        "actual plain-shared game and output images have no keyed interface");
    Image capturedMsaa,capturedFinal;
    check(capturedMsaa.prepare(device.value,width,height,false,4)&&capturedFinal.prepare(device.value,width,height,true),"private alpha targets prepared cold");
    Reference<ID3D11DeviceContext1> ranges;
    check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(ranges.out()))),"native logical constant range interface");
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;
    rasterDesc.MultisampleEnable=TRUE;rasterDesc.DepthClipEnable=TRUE;
    Reference<ID3D11RasterizerState> raster;
    check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,raster.out())),"native sample positions and raster state");context->RSSetState(raster.value);
    const D3D11_VIEWPORT full{0,0,float(width),float(height),0,1},fractional{.25f,.5f,float(width)-1,float(height)-1,0,1};
    const FLOAT zero[4]{};std::vector<std::uint8_t> input(width*height*4);
    unsigned changedByNaive=0;
    for(unsigned bytes:{64u,80u})for(UINT rangeOffset:{0u,256u}){
        for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17+bytes)%256;input[i*4+1]=UINT(i*39+rangeOffset)%256;
            input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
        context->UpdateSubresource(source.image.value,0,nullptr,input.data(),width*4,0);
        context->OMSetRenderTargets(1,&x.target.value,nullptr);context->PSSetShaderResources(0,1,&source.source.value);
        context->PSSetShader(xProgram.value,nullptr,0);context->RSSetViewports(1,&full);context->OMSetBlendState(nullptr,nullptr,UINT_MAX);draw(context.value,4,0);
        std::vector<float> words(rangeOffset?128:bytes/4);words[rangeOffset/4+4]=words[rangeOffset/4+5]=words[rangeOffset/4+6]=1;
        bd={};bd.ByteWidth=UINT(words.size()*4);bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{words.data(),0,0};Reference<ID3D11Buffer> cb;
        check(SUCCEEDED(device->CreateBuffer(&bd,&initial,cb.out())),"64/80-byte scene constants, also at nonzero logical offsets");
        UINT first=rangeOffset/16,number=rangeOffset?16:bytes/16;
        context->PSSetConstantBuffers(0,1,&cb.value);if(rangeOffset)ranges->PSSetConstantBuffers1(0,1,&cb.value,&first,&number);
        UINT sceneFirst=0,sceneNumber=0;Reference<ID3D11Buffer> sceneBound;
        ranges->PSGetConstantBuffers1(0,1,sceneBound.out(),&sceneFirst,&sceneNumber);
        context->RSSetViewports(1,&fractional);context->OMSetBlendState(nullptr,nullptr,0xbu);
        for(auto* target:{&normalMsaa,&pairedMsaa,&referenceMsaa,&naiveMsaa})context->ClearRenderTargetView(target->target.value,zero);
        check(capturedMsaa.clear(context.value),"only an explicit known clear supplies this fixture's private untouched region");
        context->PSSetShaderResources(0,1,&x.source.value);context->PSSetShader(programs[0].value,nullptr,0);
        context->OMSetRenderTargets(1,&normalMsaa.target.value,nullptr);draw(context.value,4,0);
        context->PSSetShader(programs[1].value,nullptr,0);context->OMSetRenderTargets(1,&referenceMsaa.target.value,nullptr);draw(context.value,4,0);
        context->PSSetShader(programs[0].value,nullptr,0);context->OMSetRenderTargets(1,&naiveMsaa.target.value,nullptr);
        check(operation.nativeAlpha(context.value,programs[0].value,bytes,false,draw,4,0),"counterexample executes the scene-only carrier primitive");
        context->OMSetRenderTargets(1,&pairedMsaa.target.value,nullptr);const auto before=nativeDrawCalls;
        check(operation.nativeCapture(context.value,capturedMsaa,programs[0].value,programs[2].value,draw,4,0)&&nativeDrawCalls==before+1,
            "MRT captures genuine alpha in exactly one original scene operation");
        Reference<ID3D11Buffer> bound;UINT gotFirst=0,gotNumber=0;ranges->PSGetConstantBuffers1(0,1,bound.out(),&gotFirst,&gotNumber);
        check(bound.value==cb.value&&gotFirst==sceneFirst&&gotNumber==sceneNumber,"MRT leaves the native scene constant buffer and logical range unchanged");
        for(unsigned policy:{0u,1u}){
            std::array<float,128> flags{};flags[rangeOffset/4]=float(policy);
            flags[rangeOffset/4+4]=.2f;flags[rangeOffset/4+5]=.3f;flags[rangeOffset/4+6]=.4f;
            bd.ByteWidth=rangeOffset?512:32;D3D11_SUBRESOURCE_DATA policyData{flags.data(),0,0};Reference<ID3D11Buffer> policyBuffer;
            check(SUCCEEDED(device->CreateBuffer(&bd,&policyData,policyBuffer.out())),"unchanged real resolve policy buffer");
            first=rangeOffset/16;number=rangeOffset?16:2;
            context->PSSetConstantBuffers(2,1,&policyBuffer.value);if(rangeOffset)ranges->PSSetConstantBuffers1(2,1,&policyBuffer.value,&first,&number);
            UINT policyFirst=0,policyNumber=0;Reference<ID3D11Buffer> policyBound;
            ranges->PSGetConstantBuffers1(2,1,policyBound.out(),&policyFirst,&policyNumber);
            context->RSSetViewports(1,&full);context->OMSetBlendState(nullptr,nullptr,UINT_MAX);
            context->PSSetShader(programs[3].value,nullptr,0);context->PSSetShaderResources(0,1,&normalMsaa.source.value);
            context->OMSetRenderTargets(1,&normalFinal.target.value,nullptr);draw(context.value,4,0);
            context->PSSetShaderResources(0,1,&naiveMsaa.source.value);context->OMSetRenderTargets(1,&naiveFinal.target.value,nullptr);draw(context.value,4,0);
            context->PSSetShaderResources(0,1,&pairedMsaa.source.value);context->OMSetRenderTargets(1,&pairedFinal.target.value,nullptr);
            const auto preResolve=nativeDrawCalls;
            check(!operation.nativeAlpha(context.value,programs[3].value,32,true,draw,4,0)&&nativeDrawCalls==preResolve,
                "resolve constant rewriting is explicitly excluded from the native carrier experiment");
            check(operation.nativeCapture(context.value,capturedFinal,programs[3].value,programs[5].value,draw,4,0,&capturedMsaa)&&nativeDrawCalls==preResolve+1,
                "one native resolve preserves original t0/b2 and pairs private true alpha");
            Reference<ID3D11ShaderResourceView> t1;context->PSGetShaderResources(1,1,t1.out());
            check(!t1.value,"temporary private resolve input is unbound after the original operation");
            Reference<ID3D11Buffer> restoredPolicy;
            ranges->PSGetConstantBuffers1(2,1,restoredPolicy.out(),&gotFirst,&gotNumber);
            check(restoredPolicy.value==policyBuffer.value&&gotFirst==policyFirst&&gotNumber==policyNumber,"native resolve policy and its exact range are untouched");
            const auto ordinary=pixels(device.value,context.value,normalFinal.image.value),paired=pixels(device.value,context.value,pairedFinal.image.value);
            const auto naive=pixels(device.value,context.value,naiveFinal.image.value),alpha=pixels(device.value,context.value,capturedFinal.resource());
            context->PSSetShader(programs[4].value,nullptr,0);context->PSSetShaderResources(0,1,&referenceMsaa.source.value);
            context->OMSetRenderTargets(1,&referenceFinal.target.value,nullptr);draw(context.value,4,0);
            const auto reference=pixels(device.value,context.value,referenceFinal.image.value);
            unsigned transparent=0,fractionalAlpha=0;
            for(UINT i=0;i<width*height;++i){
                check(alpha[i*4+3]==reference[i*4+3]&&!alpha[i*4]&&!alpha[i*4+1]&&!alpha[i*4+2],"same-draw MSAA coverage matches independent reference without exporting RGB");
                transparent+=alpha[i*4+3]==0;fractionalAlpha+=alpha[i*4+3]>0&&alpha[i*4+3]<255;
                for(unsigned c=0;c<4;++c)check(ordinary[i*4+c]==paired[i*4+c],"every downstream native RGBA byte stays equal with either resolve policy");
                for(unsigned c=0;c<3;++c)if(policy&&ordinary[i*4+c]!=naive[i*4+c])++changedByNaive;
            }
            check(transparent&&fractionalAlpha,"real holes, fractional alpha, MSAA sample mask and subpixel edges exercised");
            context->PSSetShader(programs[3].value,nullptr,0);
        }
    }
    check(changedByNaive>0,"negative control proves native-alpha propagation changes downstream RGB when the real resolve policy depends on alpha");
    // Original depth/stencil updates run once, in the same raster operation.
    Target depthColour;create(depthColour,1,DXGI_FORMAT_R8G8B8A8_UNORM,true);Image depthAlpha;
    check(depthAlpha.prepare(device.value,width,height),"private single-sample depth coverage target");
    D3D11_TEXTURE2D_DESC dd{};dd.Width=width;dd.Height=height;dd.MipLevels=dd.ArraySize=dd.SampleDesc.Count=1;
    dd.Format=DXGI_FORMAT_R24G8_TYPELESS;dd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    Reference<ID3D11Texture2D> depth;Reference<ID3D11DepthStencilView> dsv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_DEPTH_STENCIL_DESC ds{};ds.DepthEnable=TRUE;ds.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;ds.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
    ds.StencilEnable=TRUE;ds.StencilReadMask=ds.StencilWriteMask=255;ds.FrontFace.StencilFunc=D3D11_COMPARISON_ALWAYS;
    ds.FrontFace.StencilFailOp=ds.FrontFace.StencilDepthFailOp=D3D11_STENCIL_OP_KEEP;ds.FrontFace.StencilPassOp=D3D11_STENCIL_OP_INCR_SAT;ds.BackFace=ds.FrontFace;
    Reference<ID3D11DepthStencilState> state;
    check(SUCCEEDED(device->CreateTexture2D(&dd,nullptr,depth.out()))&&SUCCEEDED(device->CreateDepthStencilView(depth.value,&dv,dsv.out()))&&
        SUCCEEDED(device->CreateDepthStencilState(&ds,state.out())),"actual owned native depth and stencil attachments");
    context->ClearDepthStencilView(dsv.value,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,7);
    context->ClearRenderTargetView(depthColour.target.value,zero);check(depthAlpha.clear(context.value),"explicit depth control clear");
    rasterDesc.ScissorEnable=TRUE;Reference<ID3D11RasterizerState> scissor;
    check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,scissor.out())),"actual split native raster coverage");context->RSSetState(scissor.value);
    const D3D11_RECT leftHalf{0,0,LONG(width/2),LONG(height)};context->RSSetScissorRects(1,&leftHalf);context->RSSetViewports(1,&full);
    context->PSSetShader(programs[0].value,nullptr,0);context->PSSetShaderResources(0,1,&source.source.value);
    context->OMSetRenderTargets(1,&depthColour.target.value,dsv.value);context->OMSetDepthStencilState(state.value,0);
    const auto before=nativeDrawCalls;
    check(operation.nativeCapture(context.value,depthAlpha,programs[0].value,programs[2].value,draw,4,0)&&nativeDrawCalls==before+1,"original depth/stencil operation is executed once with native and private output");
    const auto depthBytes=pixels(device.value,context.value,depth.value),alpha=pixels(device.value,context.value,depthAlpha.resource());
    for(UINT i=0;i<width*height;++i){const bool visible=(i%width)<width/2;
        check(depthBytes[i*4+3]==(visible?8:7),"native stencil increments exactly once and rejected region remains untouched");
        check(alpha[i*4+3]==(visible?input[i*4+3]:0),"private coverage uses the actual native scissor/depth/stencil decisions");}
    Reference<ID3D11RenderTargetView> restoredTarget;Reference<ID3D11DepthStencilView> restoredDepth;
    context->OMGetRenderTargets(1,restoredTarget.out(),restoredDepth.out());Reference<ID3D11PixelShader> restoredShader;
    context->PSGetShader(restoredShader.out(),nullptr,nullptr);
    check(restoredTarget.value==depthColour.target.value&&restoredDepth.value==dsv.value&&restoredShader.value==programs[0].value,"original native target/depth/program restored");
    context->ClearState();
    std::printf("owned MRT chain: plain-shared non-keyed source/output; FP16 X,4x MSAA,64/80-byte ranged scene constants, both ranged resolve policies; native RGBA unchanged; true alpha; stencil once; naive carrier changes %u RGB bytes. No production image/frame permit.\n",changedByNaive);
}
void consumerControl(){
    Consumer consumer;auto& channel=consumer.channel();check(channel.page()!=nullptr,"private parent consumer channel");
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.out(),nullptr,context.out())),"independent actual parent WARP device");
    constexpr UINT width=40,height=36;std::vector<std::uint8_t> input(width*height*4);
    for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17)%256;input[i*4+1]=UINT(i*39)%256;input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    D3D11_SUBRESOURCE_DATA initial{input.data(),width*4,0};Reference<ID3D11Texture2D> source,native;
    check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out())),"parent retains exact application and native output allocations");
    Reference<IDXGIKeyedMutex> sync;
    check(SUCCEEDED(native->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(sync.out()))),"actual parent native synchronization resource");
    struct Handles {HANDLE read=nullptr,write=nullptr,process=nullptr,thread=nullptr;
        ~Handles(){if(write)CloseHandle(write);if(read)CloseHandle(read);if(process){if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)TerminateProcess(process,1);CloseHandle(process);}if(thread)CloseHandle(thread);}} handles;
    SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
    check(CreatePipe(&handles.read,&handles.write,&security,4096)&&SetHandleInformation(handles.write,HANDLE_FLAG_INHERIT,0),"owned anonymous launch pipe");
    std::array<wchar_t,32768> path{};const auto length=GetModuleFileNameW(nullptr,path.data(),UINT(path.size()));
    check(length&&length<path.size(),"exact owned executable path");
    std::wstring command=L"\""+std::wstring(path.data())+L"\" --child";
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=handles.read;
    startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION process{};
    check(CreateProcessW(path.data(),mutableCommand.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process),"launch exactly the owned producer process");
    handles.process=process.hProcess;handles.thread=process.hThread;CloseHandle(handles.read);handles.read=nullptr;
    CrossProcess message{};message.source=handle(source.value);message.native=handle(native.value);message.compositorPid=process.dwProcessId;
    check(channel.duplicateToVerifiedPeer(handles.process,message.launch),"private section duplicated to the known owned child instance");
    const std::uint64_t lease=(std::uint64_t(7)<<32)|77;channel.sourceLease(lease);
    Registry registry{};registry.sourceLease=lease;registry.revision=1;
    registry.retained[0]={message.source,21,77,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    registry.retained[1]={message.native,22,message.compositorPid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=source.value;pins[1]=native.value;
    check(channel.publishRetained(registry,pins),"parent kernel allocations publish the only source registry");
    DWORD sent=0;check(WriteFile(handles.write,&message,sizeof message,&sent,nullptr)&&sent==sizeof message,"private fixture facts sent without public mapping names");
    const auto deadline=GetTickCount64()+5000;
    while(InterlockedCompareExchange(&channel.page()->peerReady,0,0)!=1&&GetTickCount64()<deadline&&WaitForSingleObject(handles.process,0)==WAIT_TIMEOUT)Sleep(1);
    check(consumer.prepare(device.value),"consumer prepares only for its bound private peer");
    const auto& allocation=registry.retained[1];
    check(!consumer.select(device.value,allocation,message.native,lease).view,"unowned native image never selects a latest mask");
    check(sync->AcquireSync(3,5000)==S_OK,"original native consumer acquires the producer release key");
    consumer.poll(device.value);const auto selected=consumer.select(device.value,allocation,message.native,lease);
    check(selected.view&&selected.lease==lease&&selected.scenePid==77&&consumer.nativeProducer(message.compositorPid),"real cross-process pair admitted under original native ownership");
    Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> mask;selected.view->GetResource(resource.out());
    check(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(mask.out()))),"selected view is the exact imported mask allocation");
    const auto rgb=pixels(device.value,context.value,native.value),alpha=pixels(device.value,context.value,mask.value);
    for(UINT i=0;i<width*height;++i){check(alpha[i*4+3]==input[i*4+3]&&rgb[i*4+3]==255,"exact zero/fractional/opaque alpha and unchanged native alpha cross the actual process boundary");
        for(unsigned c=0;c<3;++c)check(rgb[i*4+c]==input[i*4+c]&&!alpha[i*4+c],"every native RGB byte and private alpha-only payload retained across processes");}
    auto wrong=allocation;++wrong.generation;check(!consumer.select(device.value,wrong,message.native,lease).view,"wrong native allocation generation is refused");
    wrong=allocation;++wrong.pid;check(!consumer.select(device.value,wrong,message.native,lease).view,"different actual native owner is refused");
    check(!consumer.select(device.value,allocation,message.source,lease).view,"another sync allocation cannot seal this consumer image");
    channel.sourceLease(0);check(!consumer.select(device.value,allocation,message.native,lease).view,"revoked source lease cannot select a retained pair");
    context->Flush();check(sync->ReleaseSync(0)==S_OK,"consumer releases original ownership after its exact paired copy");
    BYTE finished=1;check(WriteFile(handles.write,&finished,1,&sent,nullptr)&&sent==1,"child may retire only after parent copy acknowledgement");
    check(WaitForSingleObject(handles.process,5000)==WAIT_OBJECT_0,"owned producer exits after acknowledgement");DWORD result=1;
    check(GetExitCodeProcess(handles.process,&result)&&result==0,"actual child producer completed its native path");
    consumer.stop();
}

}
int main(int argc,char** argv){try{
    if(argc==2&&!std::strcmp(argv[1],"--child")){
        CrossProcess message{};DWORD received=0;
        check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&message,sizeof message,&received,nullptr)&&received==sizeof message,"owned launch pipe payload");
        ChannelPeer peer;check(peer.open(message.launch),"child binds the actual private kernel channel");run(&message,peer.page());
    }else if(argc==2&&!std::strcmp(argv[1],"--native-single"))nativeSingleDrawControl();
    else if(argc==2&&!std::strcmp(argv[1],"--native-mrt"))nativeMrtControl();
    else if(argc==2&&!std::strcmp(argv[1],"--consumer"))consumerControl();
    else if(argc==2&&!std::strcmp(argv[1],"--float"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_FLOAT);
    else if(argc==2&&!std::strcmp(argv[1],"--typeless"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_TYPELESS);
    else if(argc==2&&!std::strcmp(argv[1],"--pressure"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_TYPELESS,true);
    else if(argc==2&&!std::strcmp(argv[1],"--padded"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_TYPELESS,false,true);
    else if(argc==2&&!std::strcmp(argv[1],"--padded-depth"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_TYPELESS,false,true,true);
    else if(argc==2&&!std::strcmp(argv[1],"--reproject-depth"))run(nullptr,nullptr,DXGI_FORMAT_R16G16B16A16_TYPELESS,false,false,true,true);
    else if(argc==2&&!std::strcmp(argv[1],"--dxgi-sync"))run(nullptr,nullptr,DXGI_FORMAT_UNKNOWN,false,false,false,false,true);
    else {check(argc==1,"known fixture mode");run();}
    std::printf("PASS %u production alpha ancestry/ownership/pixel checks; no headset result\n",checks);return 0;}
catch(const std::exception& error){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,error.what());return 1;}}
