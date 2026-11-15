#include "pc/stream-bridge-projected-alpha-producer.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace stream_bridge::sender;
using namespace stream_bridge::sender::projected_alpha;
namespace {
unsigned checks=0;Producer* active=nullptr;
void check(bool okay,const char* why){++checks;if(!okay)throw std::runtime_error(why);}
void releasing(const MutexLease& lease,std::uint64_t key,void*)noexcept{if(active)active->releasing(lease,key);}
void released(const MutexLease& lease,HRESULT result,void*)noexcept{if(active)active->released(lease,result);}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* context,UINT count,UINT start){context->Draw(count,start);}
constexpr char ProducerFixture[]=R"(
cbuffer Scene : register(b0) {float4 words[4];};
Texture2D<float4> scene : register(t0);
float4 vs(float2 uv:TEXCOORD0):SV_Position{return float4(2*uv-1,0,1);}
float4 ps(float4 xy:SV_Position):SV_Target{
    float4 value=scene.Load(int3(int2(xy.xy),0));
    value.rgb*=words[1].xyz;
    value.a=words[3].y==0?1:value.a;
    return value;
}
)";
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
void run(){
    auto vsCode=compile("vs","vs_5_0"),psCode=compile("ps","ps_5_0");
    std::array<ShaderFingerprint,2> contracts{};contracts[0].kind=NativeShader::StreamVertex;contracts[1].kind=NativeShader::MultiViewInner;
    check(dxbcFingerprint(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),contracts[0].hash)&&
        dxbcFingerprint(psCode->GetBufferPointer(),psCode->GetBufferSize(),contracts[1].hash),"owned shader contracts");
    NativeShaders shaders(contracts.data(),contracts.size(),nullptr,nullptr,true);
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,
        D3D11_SDK_VERSION,device.out(),nullptr,context.out()))&&shaders.observeDevice(device.value),"owned actual native-style device");
    MutexObserver locks({nullptr,releasing,released,nullptr});check(locks.prepare(device.value),"original native synchronization observed");
    ChannelParent channel;check(channel.create(),"private producer channel");
    const unsigned policyBytes=64;Producer producer(&contracts[1],&policyBytes,1);active=&producer;
    check(producer.prepare(device.value,channel.page(),shaders,locks),"production controller prepares embedded own kernels");
    constexpr UINT width=40,height=36;constexpr std::uint32_t scenePid=77,compositorPid=88;
    const std::uint64_t lease=(std::uint64_t(7)<<32)|scenePid;channel.sourceLease(lease);
    std::vector<std::uint8_t> input(width*height*4);
    for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17)%256;input[i*4+1]=UINT(i*39)%256;input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;D3D11_SUBRESOURCE_DATA initial{input.data(),width*4,0};
    Reference<ID3D11Texture2D> source,native;Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11RenderTargetView> target;
    check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out()))&&
        SUCCEEDED(device->CreateShaderResourceView(source.value,nullptr,sourceView.out()))&&SUCCEEDED(device->CreateRenderTargetView(native.value,nullptr,target.out())),"actual owned source and native target");
    producer.target(target.value);const auto sourceHandle=handle(source.value),nativeHandle=handle(native.value);
    MaskRegistration prepared{};check(channel.page()->registrations[0].read(prepared)&&prepared.maskHandle&&producer.stage()==2,"cold preparation published the actual companion allocation");
    Registry registry{};registry.sourceLease=lease;registry.revision=1;
    registry.retained[0]={sourceHandle,21,scenePid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    registry.retained[1]={nativeHandle,22,compositorPid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=source.value;pins[1]=native.value;
    check(channel.publishRetained(registry,pins),"real held source registry publication");
    Reference<ID3D11VertexShader> vs;Reference<ID3D11PixelShader> ps;Reference<ID3D11InputLayout> layout;Reference<ID3D11Buffer> vertices,constants;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out())),"actual observed native-model program objects");
    check(shaders.identify(vs.value)==NativeShader::StreamVertex&&shaders.identify(ps.value)==NativeShader::MultiViewInner,"actual shader observations survive cold companion preparation");
    const D3D11_INPUT_ELEMENT_DESC element{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    check(SUCCEEDED(device->CreateInputLayout(&element,1,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.out())),"actual input layout");
    producer.geometry().layout(&element,1,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.value);
    const std::array<float,8> points{{0,0,0,1,1,0,1,1}};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof points;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData{points.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&vertexData,vertices.out())),"actual vertex buffer");
    producer.geometry().buffer(&buffer,&vertexData,vertices.value);
    std::array<float,16> words{};words[4]=words[5]=words[6]=1;buffer.ByteWidth=sizeof words;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData{words.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&constantData,constants.out())),"native alpha policy constant buffer");
    context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps.value,nullptr,0);context->PSSetConstantBuffers(0,1,&constants.value);
    context->IASetInputLayout(layout.value);UINT stride=8,offset=0;context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->OMSetRenderTargets(1,&target.value,nullptr);context->PSSetShaderResources(0,1,&sourceView.value);
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    Reference<IDXGIKeyedMutex> sourceMutex,streamMutex;
    check(SUCCEEDED(source->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(sourceMutex.out())))&&
        SUCCEEDED(native->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(streamMutex.out()))),"original application and stream mutexes");
    check(sourceMutex->AcquireSync(0,0)==S_OK&&streamMutex->AcquireSync(0,0)==S_OK,"actual producer ownership");
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"production bound vertex range proves full image coverage");
    check(bool(locks.current(device.value,sourceHandle)),"actual source-allocation ownership is retained");
    producer.draw(context.value,draw,4,0);draw(context.value,4,0);context->Flush();
    std::printf("production stage after draw=%u\n",producer.stage());
    check(streamMutex->ReleaseSync(3)==S_OK,"native stream ownership transferred with its unchanged release key");
    std::printf("production stage after release=%u\n",producer.stage());
    ImageWitness witness{};check(channel.page()->witnesses[0].read(witness)&&witness.complete&&witness.sourceLease==lease&&
        witness.scene.pid==scenePid&&witness.native.pid==compositorPid&&witness.native.generation==22,"production witness separates authenticated game ancestry from actual compositor ownership");
    bool sealed=false;for(const auto& release:witness.releases)sealed|=release.allocation==nativeHandle&&release.key==3&&release.scope>1;
    check(sealed,"witness sealed by the original native output release");
    Reference<ID3D11Texture2D> mask;check(SUCCEEDED(device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(witness.maskHandle)),
        __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(mask.out()))),"import actual production mask");
    check(streamMutex->AcquireSync(3,0)==S_OK,"actual consumer owns the exact image version");
    const auto nativePixels=pixels(device.value,context.value,native.value),maskPixels=pixels(device.value,context.value,mask.value);
    for(UINT i=0;i<width*height;++i){check(nativePixels[i*4+3]==255,"ordinary native alpha policy remains opaque");
        check(maskPixels[i*4+3]==input[i*4+3],"production mask preserves every zero/fractional/opaque source alpha");
        for(unsigned c=0;c<3;++c)check(nativePixels[i*4+c]==input[i*4+c]&&!maskPixels[i*4+c],"native RGB including additive zero-alpha colour unchanged; no RGB exported");}
    producer.invalidate(native.value);check(channel.page()->witnesses[0].read(witness)&&!witness.complete,"unsupported native write revokes the previously sealed image before reuse");
    check(streamMutex->ReleaseSync(0)==S_OK&&streamMutex->AcquireSync(0,0)==S_OK,"next exact native ownership interval");
    channel.sourceLease(lease+0x100000000ull);producer.draw(context.value,draw,4,0);draw(context.value,4,0);
    check(streamMutex->ReleaseSync(0)==S_OK&&channel.page()->witnesses[0].read(witness)&&!witness.complete,"revoked app session cannot republish old coverage");
    check(sourceMutex->ReleaseSync(0)==S_OK,"native application ownership ends");
    active=nullptr;context->ClearState();
}
}
int main(){try{run();std::printf("PASS %u production alpha ancestry/ownership/pixel checks; no headset result\n",checks);return 0;}
catch(const std::exception& error){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,error.what());return 1;}}
