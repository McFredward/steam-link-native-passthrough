// Real WARP and entirely owned shaders/textures. No native DLL or shader input.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include "pc/stream-bridge-sender-provider-shaders.h"
#include "pc/stream-bridge-sender-provider-coverage-render.h"
#include "pc/stream-bridge-native-composition.h"
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
using namespace stream_bridge::sender;
namespace {
unsigned checks=0,drawCalls=0;UINT drawCount=0,drawStart=0;NativeDraw originalDraw=nullptr;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class T>struct Com {
    T* p=nullptr;Com()=default;~Com(){if(p)p->Release();}
    Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    Com(Com&& other)noexcept:p(std::exchange(other.p,nullptr)){}
    T** out(){return &p;}T* operator->(){return p;}
};
template<class T,class U>T pointer(U value){static_assert(sizeof(T)==sizeof(U),"Win64 pointers");T result{};std::memcpy(&result,&value,sizeof result);return result;}
void STDMETHODCALLTYPE countedDraw(ID3D11DeviceContext* ctx,UINT count,UINT start){++drawCalls;drawCount=count;drawStart=start;originalDraw(ctx,count,start);}
ULONG STDMETHODCALLTYPE ownedRef(ID3D11Device*){return 1;}
UINT STDMETHODCALLTYPE ownedFlags(ID3D11Device*){return 0;}
constexpr unsigned Width=64,Height=64;
constexpr char Shader[]=R"(
cbuffer Parameters : register(b2) { float4 unused0; float4 unused1; float4 offset; };
Texture2D<float4> scene : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p=float2((id<<1)&2,id&2);
    return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
float4 copy(float4 p : SV_Position) : SV_Target {
    float3 rgb=scene.Load(int3(int2(p.xy-offset.xy),0)).rgb;
    return float4(rgb,1);
}
float4 other(float4 p : SV_Position) : SV_Target { return float4(0.2,0.3,0.4,1); }
)";
Com<ID3DBlob> compile(const char* entry,const char* profile){
    Com<ID3DBlob> code;Com<ID3DBlob> errors;
    const auto result=D3DCompile(Shader,sizeof Shader-1,"sfnp-owned-composition-fixture",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),errors.out());
    if(FAILED(result)&&errors.p)std::fwrite(errors->GetBufferPointer(),1,errors->GetBufferSize(),stderr);
    check(SUCCEEDED(result),"owned GPU fixture compiles");return code;
}
std::vector<std::uint8_t> pixels(ID3D11DeviceContext* ctx,ID3D11Texture2D* source,ID3D11Texture2D* staging,unsigned pixelBytes){
    ctx->CopyResource(staging,source);D3D11_MAPPED_SUBRESOURCE data{};
    check(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&data)),"actual fixture readback");
    std::vector<std::uint8_t> result(Width*Height*pixelBytes);
    for(unsigned y=0;y<Height;++y)std::memcpy(result.data()+y*Width*pixelBytes,static_cast<const std::uint8_t*>(data.pData)+y*data.RowPitch,Width*pixelBytes);
    ctx->Unmap(staging,0);return result;
}
void run(){
    auto vsCode=compile("vs","vs_5_0"),psCode=compile("copy","ps_5_0"),otherCode=compile("other","ps_5_0");
    std::array<ShaderFingerprint,2> contracts{};contracts[0].kind=NativeShader::FullscreenVertex;contracts[1].kind=NativeShader::CopyView;
    check(dxbcFingerprint(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),contracts[0].hash)&&dxbcFingerprint(psCode->GetBufferPointer(),psCode->GetBufferSize(),contracts[1].hash),"own actual executable/signature contracts");
    NativeShaders observer(contracts.data(),contracts.size());
    HMODULE runtime=LoadLibraryW(L"d3d11.dll");check(runtime!=nullptr,"public D3D11 runtime");
    static void* factorySlot=nullptr;factorySlot=pointer<void*>(GetProcAddress(runtime,"D3D11CreateDevice"));const auto realFactory=factorySlot;
    check(observer.arm(reinterpret_cast<std::uintptr_t>(&factorySlot)),"actual public device factory import is observed");
    using Factory=decltype(&D3D11CreateDevice);const auto cachedFactory=pointer<Factory>(factorySlot);
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0;D3D_FEATURE_LEVEL obtained{};
    check(SUCCEEDED(cachedFactory(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&requested,1,D3D11_SDK_VERSION,device.out(),&obtained,context.out())),"original public WARP factory succeeds unchanged");
    check(obtained==requested&&observer.stats().devices==1&&observer.stats().factory,"shader observation is installed before the native device returns");
    check(device->GetFeatureLevel()==obtained&&device->GetCreationFlags()==0,"ordinary native device getters remain valid after observation");
    Com<ID3D11Device> parent;context->GetDevice(parent.out());check(parent.p&&parent->GetFeatureLevel()==obtained,"context-returned native device retains its complete dispatch");
    Com<ID3D11Device> independent;Com<ID3D11DeviceContext> independentContext;
    check(SUCCEEDED(pointer<Factory>(realFactory)(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,&requested,1,D3D11_SDK_VERSION,independent.out(),nullptr,independentContext.out())),"unobserved device sharing native methods still creates normally");
    const auto independentTable=*reinterpret_cast<void***>(independent.p);
    Com<ID3D11PixelShader> independentShader;
    check(SUCCEEDED(independent->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,independentShader.out()))&&observer.identify(independentShader.p)==NativeShader::Unknown&&observer.stats().devices==1,"shared method observation forwards without granting an unobserved device shader authority");
    check(observer.observeDevice(independent.p)&&*reinterpret_cast<void***>(independent.p)==independentTable&&observer.stats().devices==2,"second observed device preserves its entire native method table");
    Com<ID3D11PixelShader> observedIndependent;
    check(SUCCEEDED(independent->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,observedIndependent.out()))&&observer.identify(observedIndependent.p)==NativeShader::CopyView,"actual shader creation on the second observed device is recognized");
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps,unknown,coverage;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out())),"actual native VS creation forwarded");
    check(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out())),"actual native PS creation forwarded");
    check(SUCCEEDED(device->CreatePixelShader(otherCode->GetBufferPointer(),otherCode->GetBufferSize(),nullptr,unknown.out())),"unrecognized shader still created normally");
    check(observer.identify(vs.p)==NativeShader::FullscreenVertex&&observer.identify(ps.p)==NativeShader::CopyView&&observer.identify(unknown.p)==NativeShader::Unknown,"recognition belongs to the actual created COM objects");
    check(createCopyViewCoverageShader(device.p,coverage.out()),"production alpha-only coverage shader compiled cold");
    const auto originalDeviceTable=*reinterpret_cast<void***>(device.p);const auto cachedPixel=pointer<HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**)>(originalDeviceTable[15]);
    originalDraw=pointer<NativeDraw>((*reinterpret_cast<void***>(context.p))[13]);
    std::array<Com<ID3D11Texture2D>,2> sources;std::array<Com<ID3D11ShaderResourceView>,2> views;
    std::array<std::vector<std::uint8_t>,2> sourceBytes;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=Width;desc.Height=Height/2;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const std::uint8_t alpha[]{0,64,128,191,255};
    for(unsigned eye=0;eye<2;++eye){auto& bytes=sourceBytes[eye];bytes.resize(Width*Height/2*4);
        for(unsigned y=0;y<Height/2;++y)for(unsigned x=0;x<Width;++x){const auto at=(y*Width+x)*4;bytes[at]=std::uint8_t(x*4);bytes[at+1]=std::uint8_t(255-x*4);bytes[at+2]=std::uint8_t(y*8);bytes[at+3]=alpha[(x+3*y+eye)%5];}
        D3D11_SUBRESOURCE_DATA input{bytes.data(),Width*4,0};
        check(SUCCEEDED(device->CreateTexture2D(&desc,&input,sources[eye].out()))&&SUCCEEDED(device->CreateShaderResourceView(sources[eye].p,nullptr,views[eye].out())),"real independent per-eye source resources");
    }
    Com<ID3D11Texture2D> rgb,rgbRead,mask,maskRead;Com<ID3D11RenderTargetView> rgbTarget,maskTarget;
    desc.Height=Height;desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,rgb.out()))&&SUCCEEDED(device->CreateRenderTargetView(rgb.p,nullptr,rgbTarget.out())),"actual FP16 RGB intermediate");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,rgbRead.out())),"FP16 fixture readback");
    desc.Format=DXGI_FORMAT_R8_UNORM;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,maskRead.out())),"R8 fixture readback");
    desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_RENDER_TARGET;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,mask.out()))&&SUCCEEDED(device->CreateRenderTargetView(mask.p,nullptr,maskTarget.out())),"independent full-resolution coverage target");
    Com<ID3D11Buffer> cb;D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=48;buffer.Usage=D3D11_USAGE_DEFAULT;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(SUCCEEDED(device->CreateBuffer(&buffer,nullptr,cb.out())),"native copy-view constant buffer");
    Com<ID3D11RasterizerState> raster;D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;check(SUCCEEDED(device->CreateRasterizerState(&rd,raster.out())),"native rasterizer");
    context->VSSetShader(vs.p,nullptr,0);context->PSSetShader(ps.p,nullptr,0);context->PSSetConstantBuffers(2,1,&cb.p);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->RSSetState(raster.p);
    context->OMSetRenderTargets(1,&rgbTarget.p,nullptr);context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    const FLOAT white[]{1,1,1,1},black[4]{};context->ClearRenderTargetView(rgbTarget.p,black);context->ClearRenderTargetView(maskTarget.p,white);
    NativeComposition graph;check(graph.begin(17,23,Width,Height,3)&&graph.image(100),"exact fixture composition");
    for(unsigned tile=0;tile<4;++tile){const auto eye=tile/2;
        float constants[12]{};constants[9]=float(eye*Height/2);context->UpdateSubresource(cb.p,0,nullptr,constants,0,0);context->PSSetShaderResources(0,1,&views[eye].p);
        const D3D11_VIEWPORT viewport{0,float(tile*Height/4),float(Width),float(Height/4),0,1};context->RSSetViewports(1,&viewport);
        context->Draw(3,0);const auto before=pixels(context.p,rgb.p,rgbRead.p,8);drawCalls=0;
        check(drawCopyViewCoverage(context.p,maskTarget.p,coverage.p,countedDraw,3,0),"production extraction before opaque-alpha intermediate conversion");
        check(drawCalls==1&&drawCount==3&&!drawStart,"same native Draw arguments are forwarded once for the companion target");
        check(before==pixels(context.p,rgb.p,rgbRead.p,8),"all FP16 RGB bytes remain unchanged");
        Com<ID3D11PixelShader> actualPS;Com<ID3D11VertexShader> actualVS;Com<ID3D11RenderTargetView> actualRT;Com<ID3D11Buffer> actualCB;Com<ID3D11ShaderResourceView> actualSource;
        context->PSGetShader(actualPS.out(),nullptr,nullptr);context->VSGetShader(actualVS.out(),nullptr,nullptr);context->OMGetRenderTargets(1,actualRT.out(),nullptr);context->PSGetConstantBuffers(2,1,actualCB.out());context->PSGetShaderResources(0,1,actualSource.out());
        check(actualPS.p==ps.p&&actualVS.p==vs.p&&actualRT.p==rgbTarget.p&&actualCB.p==cb.p&&actualSource.p==views[eye].p,"native PS, VS, destination, constants and actual per-eye SRV are retained");
        check(graph.draw(1u<<eye,{0,tile*Height/4,Width,Height/4}),"two exact non-overlapping native views for each actual eye");
    }
    const auto transmittance=pixels(context.p,mask.p,maskRead.p,1),original=pixels(context.p,rgb.p,rgbRead.p,8);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){const unsigned eye=y/(Height/2),localY=y%(Height/2);const auto a=sourceBytes[eye][(localY*Width+x)*4+3];
        check(transmittance[y*Width+x]==255-a,"zero, fractional and opaque source coverage survives opaque intermediate alpha independently of RGB");
        std::uint16_t halfAlpha=0;std::memcpy(&halfAlpha,original.data()+(y*Width+x)*8+6,2);check(halfAlpha==0x3c00,"native fixture actually destroyed source alpha by writing FP16 one");
    }
    check(graph.complete()&&graph.chroma(100,200)&&graph.pack(200,300)&&graph.luma(100,300)&&graph.ready(17,23,300),"complete native graph reaches exact planar output");
    check(!drawCopyViewCoverage(context.p,maskTarget.p,coverage.p,countedDraw,4,0)&&!drawCopyViewCoverage(context.p,maskTarget.p,coverage.p,countedDraw,3,1),"unreviewed copy-view geometry refuses without drawing");
    // Owned public-method admission control, with no native executable writes.
    // Keep its storage valid for the process-lifetime cached-callback records.
    static struct {void** table;} retryDevice{};
    retryDevice.table=static_cast<void**>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(retryDevice.table!=nullptr,"owned method-table admission fixture");
    for(unsigned slot=0;slot<43;++slot)retryDevice.table[slot]=pointer<void*>(&ownedRef);
    retryDevice.table[38]=pointer<void*>(&ownedFlags);DWORD protection=0;
    check(VirtualProtect(retryDevice.table,4096,PAGE_EXECUTE_READ,&protection)!=0,"owned executable-page refusal control");
    auto* retryObject=reinterpret_cast<ID3D11Device*>(&retryDevice);
    check(!observer.observeDevice(retryObject)&&observer.stats().devices==2,"executable method-table pages refuse without device publication");
    check(VirtualProtect(retryDevice.table,4096,PAGE_READONLY,&protection)!=0&&observer.observeDevice(retryObject)&&observer.stats().devices==3,"failed admission leaves a clean slot for a valid retry of the same object and table");
    observer.stop();check(factorySlot==realFactory,"public factory import restored to its original owner");
    MEMORY_BASIC_INFORMATION retryMemory{};
    check(retryDevice.table[12]==pointer<void*>(&ownedRef)&&retryDevice.table[15]==pointer<void*>(&ownedRef)&&VirtualQuery(retryDevice.table,&retryMemory,sizeof retryMemory)&&retryMemory.Protect==PAGE_READONLY,"stop restores only owned method cells and original page protection");
    check(observer.identify(ps.p)==NativeShader::Unknown,"stopped observation grants no shader permission");
    Com<ID3D11PixelShader> after;check(SUCCEEDED(cachedPixel(device.p,psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,after.out())),"cached native shader callback remains a valid forwarding function after stop");
}
}
int main(){try{run();std::printf("native composition WARP: %u checks PASS\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"composition GPU failed after %u: %s\n",checks,e.what());return 1;}}
