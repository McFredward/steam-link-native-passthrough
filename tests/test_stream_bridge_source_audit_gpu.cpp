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
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
void STDMETHODCALLTYPE fakeDraw(ID3D11DeviceContext*,UINT count,UINT start){++nativeDraws;lastCount=count;lastStart=start;SetLastError(1234);}
void STDMETHODCALLTYPE nextDraw(ID3D11DeviceContext*,UINT count,UINT start){++nativeDraws;lastCount=count;lastStart=start;SetLastError(4321);}
HRESULT STDMETHODCALLTYPE fakeQuery(ID3D11DeviceContext*,REFIID,void** out){*out=nullptr;return E_NOINTERFACE;}
ULONG STDMETHODCALLTYPE fakeRef(ID3D11DeviceContext*){return 1;}
void STDMETHODCALLTYPE noShader(ID3D11DeviceContext*,ID3D11PixelShader** out,ID3D11ClassInstance**,UINT*){*out=nullptr;SetLastError(999);}
ID3D11DeviceContext* fakeContext=nullptr;
void STDMETHODCALLTYPE getFakeContext(ID3D11Device*,ID3D11DeviceContext** out){*out=fakeContext;}
void dispatchControls(){
    static std::array<void*,116> storage{};auto* table=storage.data()+1;
    for(unsigned i=0;i<115;++i)table[i]=bits<void*>(&fakeRef);
    table[0]=bits<void*>(&fakeQuery);table[13]=bits<void*>(&fakeDraw);table[74]=bits<void*>(&noShader);
    static struct {void** table;} object{table};fakeContext=reinterpret_cast<ID3D11DeviceContext*>(&object);
    static std::array<void*,43> deviceTable{};deviceTable[40]=bits<void*>(&getFakeContext);
    static struct {void** table;} device{deviceTable.data()};
    auto* observer=new Observer(Launch{});Observer::instance.store(observer);observer->attachContext(reinterpret_cast<ID3D11Device*>(&device));
    check(observer->status.state==5&&observer->contexts[0].count==115,"owned base-context ABI admission");
    const auto cached=bits<Draw>(observer->contexts[0].facade[13]);nativeDraws=0;
    fakeContext->Draw(17,9);check(nativeDraws==1&&lastCount==17&&lastStart==9&&GetLastError()==1234,"native arguments and LastError survive diagnostic getters exactly once");
    table[13]=bits<void*>(&nextDraw);fakeContext->Draw(19,7);
    check(nativeDraws==2&&lastCount==19&&lastStart==7&&GetLastError()==4321,"forwarding follows changed current native dispatch without a stale target");
    observer->stopped=true;auto& ctx=observer->contexts[0];ctx.facade[1]=bits<void*>(&fakeRef);observer->detach();
    check(object.table==ctx.facade,"stop preserves a separately modified object-local facade");
    ctx.facade[1]=ctx.expected[1];observer->detach();
    check(object.table==table,"stop restores only an entirely owned facade");
    cached(fakeContext,23,5);check(nativeDraws==3&&lastCount==23&&lastStart==5&&GetLastError()==4321,"cached callback forwards through the current native target after stop");
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
    auto* observer=new Observer(Launch{},contracts.data(),contracts.size());Observer::instance.store(observer);
    check(observer->shaders.observeDevice(device.p)&&observer->status.state==5,"same-caller context observation permits SINGLETHREADED without cross-thread D3D access");
    auto& dispatch=observer->contexts[0];
    check(dispatch.original==originalTable&&dispatch.owns(),"object-local dispatch retains the native table and all advertised public methods");
    for(unsigned i=0;i<dispatch.count;++i)if(i!=13)check(dispatch.function(i)==reinterpret_cast<std::uintptr_t>(originalTable[i]),"every public forwarder resolves its current native method");
    const auto cached=bits<Draw>(dispatch.facade[13]);
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
    Com<ID3D11BlendState> additive;D3D11_BLEND_DESC blend{};auto& targetBlend=blend.RenderTarget[0];
    targetBlend.BlendEnable=TRUE;targetBlend.SrcBlend=targetBlend.DestBlend=targetBlend.SrcBlendAlpha=targetBlend.DestBlendAlpha=D3D11_BLEND_ONE;
    targetBlend.BlendOp=targetBlend.BlendOpAlpha=D3D11_BLEND_OP_ADD;targetBlend.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    check(SUCCEEDED(device.p->CreateBlendState(&blend,additive.out())),"additive output makes duplicate native draws observable");
    context.p->OMSetBlendState(additive.p,nullptr,0xffffffffu);const FLOAT zero[4]{};context.p->ClearRenderTargetView(rtv.p,zero);
    bits<Draw>(dispatch.function(13))(context.p,3,0);const auto baseline=pixels(context.p,target.p,staging.p);
    context.p->ClearRenderTargetView(rtv.p,zero);context.p->Draw(3,0);
    check(observer->draws==1&&dispatch.owns(),"native first-use specialization preserves object-local observation");
    check(baseline==pixels(context.p,target.p,staging.p),"observer leaves every native output byte unchanged");
    check(observer->status.sampleCount==2&&observer->recognized==1,"bounded observations contain both actual source slots");
    for(unsigned i=0;i<2;++i){const auto& s=observer->status.samples[i];check(s.slot==i&&s.pid==101+i&&s.ownerMatches==1&&s.width==W&&s.height==H&&s.outputWidth==W&&s.outputHeight==H,"bound source ownership and arbitrary physical extents come from actual views");}
    // Contention must drop diagnostics instead of waiting on a render thread.
    const auto drops=observer->dropped.load();std::atomic<bool> locked{false},done{false};
    std::thread cold([&]{std::lock_guard<std::mutex> guard(observer->mutex);locked=true;while(!done)SwitchToThread();});
    while(!locked)SwitchToThread();
    context.p->Draw(3,0);done=true;cold.join();
    check(observer->dropped==drops+1&&observer->draws==2,"cold diagnostic contention never blocks or duplicates native drawing");
    Com<ID3D11Device> foreign;Com<ID3D11DeviceContext> foreignContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,foreign.out(),nullptr,foreignContext.out())),"independent unobserved native device");
    const auto observations=observer->recognized.load();foreignContext.p->Draw(0,0);
    check(observer->recognized==observations&&observer->draws==2,"other contexts sharing the native table only forward");
    observer->stopped=true;observer->detach();observer->shaders.stop();
    check(*reinterpret_cast<void***>(context.p)==originalTable,"stop restores the exact owned object vptr without changing its native table");
    const auto serial=observer->serial;context.p->ClearRenderTargetView(rtv.p,zero);cached(context.p,3,0);
    check(observer->draws==3&&observer->serial==serial,"cached callback remains a native-only forwarder after stop");
    check(baseline==pixels(context.p,target.p,staging.p),"stop preserves all native RGBA output bytes");

}
}
int main(){try{dispatchControls();run();std::printf("source audit owned WARP: %u checks PASS\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"source audit failed after %u: %s\n",checks,e.what());return 1;}}
