// Real WARP, owned shaders/images and the production metadata observer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pc/stream-bridge-source-audit-observer.cpp"
#include <d3dcompiler.h>
#include <cstdio>
#include <stdexcept>
using namespace stream_bridge::sender;
using namespace stream_bridge::sender::source_audit;
namespace {
unsigned checks=0,nativeDraws=0;UINT lastCount=0,lastStart=0;
using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
Draw nativeDraw=nullptr;
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
void STDMETHODCALLTYPE counted(ID3D11DeviceContext* ctx,UINT count,UINT start){
    ++nativeDraws;lastCount=count;lastStart=start;nativeDraw(ctx,count,start);SetLastError(1234);
}
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T** out(){return &p;}};
constexpr unsigned W=37,H=23;
constexpr char Program[]=R"(
Texture2D<float4> scene:register(t0);Texture2D<float4> overlay:register(t1);
float4 vs(uint id:SV_VertexID):SV_Position {float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float4 ps(float4 p:SV_Position):SV_Target{return scene.Load(int3(int2(p.xy),0))+overlay.Load(int3(int2(p.xy),0))*0.25;}
)";
std::vector<unsigned char> pixels(ID3D11DeviceContext* ctx,ID3D11Texture2D* image,ID3D11Texture2D* staging){
    ctx->CopyResource(staging,image);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(ctx->Map(staging,0,D3D11_MAP_READ,0,&mapped)),"owned output readback");
    std::vector<unsigned char> out(W*H*4);
    for(unsigned y=0;y<H;++y)std::memcpy(out.data()+y*W*4,static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch,W*4);
    ctx->Unmap(staging,0);return out;
}
void run(){
    Com<ID3DBlob> vsCode,psCode;
    check(SUCCEEDED(D3DCompile(Program,sizeof Program-1,nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,vsCode.out(),nullptr))&&
          SUCCEEDED(D3DCompile(Program,sizeof Program-1,nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,psCode.out(),nullptr)),"owned stream programs compile");
    std::array<ShaderFingerprint,2> contracts{};contracts[0].kind=NativeShader::StreamVertex;contracts[1].kind=NativeShader::StreamCombine;
    check(dxbcFingerprint(vsCode.p->GetBufferPointer(),vsCode.p->GetBufferSize(),contracts[0].hash)&&
          dxbcFingerprint(psCode.p->GetBufferPointer(),psCode.p->GetBufferSize(),contracts[1].hash),"owned actual shader identities");
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,D3D11_SDK_VERSION,device.out(),nullptr,context.out())),"single-threaded native WARP device");
    const auto originalTable=*reinterpret_cast<void***>(context.p);
    std::array<void*,115> originalEntries{};std::memcpy(originalEntries.data(),originalTable,sizeof originalEntries);
    nativeDraw=bits<Draw>(originalTable[13]);DWORD old=0,ignored=0;
    check(Observer::writable(originalTable+13,old),"owned counting shim installation");
    InterlockedExchangePointer(originalTable+13,bits<void*>(&counted));VirtualProtect(originalTable+13,sizeof(void*),old,&ignored);
    auto* observer=new Observer(Launch{},contracts.data(),contracts.size());Observer::instance.store(observer);
    check(observer->shaders.observeDevice(device.p)&&observer->status.state==5,"same-caller context observation permits SINGLETHREADED without cross-thread D3D access");
    check(*reinterpret_cast<void***>(context.p)==originalTable,"entire native vptr, private tail and RTTI are preserved");
    for(unsigned i=0;i<originalEntries.size();++i)if(i!=13)check(originalEntries[i]==originalTable[i],"all other public context methods remain native");
    const auto cached=bits<Draw>(originalTable[13]);
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;
    check(SUCCEEDED(device.p->CreateVertexShader(vsCode.p->GetBufferPointer(),vsCode.p->GetBufferSize(),nullptr,vs.out()))&&
          SUCCEEDED(device.p->CreatePixelShader(psCode.p->GetBufferPointer(),psCode.p->GetBufferSize(),nullptr,ps.out())),"native shader creation forwards");
    check(observer->shaders.identify(ps.p)==NativeShader::StreamCombine,"recognition uses the actual owned returned program");
    std::array<Com<ID3D11Texture2D>,2> sources;std::array<Com<ID3D11ShaderResourceView>,2> views;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=W;desc.Height=H;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
    observer->inventory.epoch=7;
    for(unsigned i=0;i<2;++i){std::vector<unsigned char> input(W*H*4);
        for(unsigned j=0;j<W*H;++j){input[j*4]=static_cast<unsigned char>(j%101);input[j*4+1]=43;input[j*4+2]=17;input[j*4+3]=static_cast<unsigned char>((j%5)*51);}
        D3D11_SUBRESOURCE_DATA initial{input.data(),W*4,0};
        check(SUCCEEDED(device.p->CreateTexture2D(&desc,&initial,sources[i].out()))&&SUCCEEDED(device.p->CreateShaderResourceView(sources[i].p,nullptr,views[i].out())),"owned shared source image");
        Com<IDXGIResource> dxgi;HANDLE handle=nullptr;
        check(SUCCEEDED(sources[i].p->QueryInterface(IID_IDXGIResource,reinterpret_cast<void**>(dxgi.out())))&&SUCCEEDED(dxgi.p->GetSharedHandle(&handle))&&handle,"real source handle exists");
        observer->inventory.allocations[i]={reinterpret_cast<std::uintptr_t>(handle),11+i,101+i,W,H,28,1,1};
    }
    Com<ID3D11Texture2D> target,staging;Com<ID3D11RenderTargetView> rtv;
    desc.MiscFlags=0;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
    check(SUCCEEDED(device.p->CreateTexture2D(&desc,nullptr,target.out()))&&SUCCEEDED(device.p->CreateRenderTargetView(target.p,nullptr,rtv.out())),"owned native output");
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(SUCCEEDED(device.p->CreateTexture2D(&desc,nullptr,staging.out())),"owned staging output");
    const D3D11_VIEWPORT viewport{0,0,float(W),float(H),0,1};context.p->RSSetViewports(1,&viewport);
    context.p->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context.p->VSSetShader(vs.p,nullptr,0);context.p->PSSetShader(ps.p,nullptr,0);
    ID3D11ShaderResourceView* inputs[]{views[0].p,views[1].p};context.p->PSSetShaderResources(0,2,inputs);context.p->OMSetRenderTargets(1,&rtv.p,nullptr);
    counted(context.p,3,0);const auto baseline=pixels(context.p,target.p,staging.p);nativeDraws=0;
    context.p->Draw(3,0);check(GetLastError()==1234&&nativeDraws==1&&lastCount==3&&!lastStart,"native draw executes exactly once with original arguments and LastError");
    check(baseline==pixels(context.p,target.p,staging.p),"observer leaves every native output byte unchanged");
    check(observer->status.sampleCount==2&&observer->recognized==1,"bounded observations contain both actual source slots");
    for(unsigned i=0;i<2;++i){const auto& s=observer->status.samples[i];check(s.slot==i&&s.pid==101+i&&s.ownerMatches==1&&s.width==W&&s.height==H&&s.outputWidth==W&&s.outputHeight==H,"bound source ownership and arbitrary physical extents come from actual views");}
    // Contention must drop diagnostics instead of waiting on a render thread.
    const auto drops=observer->dropped.load();std::atomic<bool> locked{false},done{false};
    std::thread cold([&]{std::lock_guard<std::mutex> guard(observer->mutex);locked=true;while(!done)SwitchToThread();});
    while(!locked)SwitchToThread();
    context.p->Draw(3,0);done=true;cold.join();
    check(observer->dropped==drops+1&&nativeDraws==2,"cold diagnostic contention never blocks or duplicates native drawing");
    Com<ID3D11Device> foreign;Com<ID3D11DeviceContext> foreignContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,foreign.out(),nullptr,foreignContext.out())),"independent unobserved native device");
    const auto observations=observer->recognized.load();foreignContext.p->Draw(0,0);
    check(observer->recognized==observations&&nativeDraws==3&&lastCount==0,"other contexts sharing the native table only forward");
    observer->stopped=true;observer->detach();observer->shaders.stop();
    check(originalTable[13]==bits<void*>(&counted),"stop restores only the exact owned method cell");
    const auto serial=observer->serial;cached(context.p,3,0);
    check(nativeDraws==4&&observer->serial==serial&&GetLastError()==1234,"cached callback remains a native-only forwarder after stop");
    check(baseline==pixels(context.p,target.p,staging.p),"stop preserves all native RGBA output bytes");
    check(Observer::writable(originalTable+13,old),"counting shim restoration");
    InterlockedExchangePointer(originalTable+13,bits<void*>(nativeDraw));VirtualProtect(originalTable+13,sizeof(void*),old,&ignored);
}
}
int main(){try{run();std::printf("source audit owned WARP: %u checks PASS\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"source audit failed after %u: %s\n",checks,e.what());return 1;}}
