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
unsigned otherCalls=0,otherCommand=0;std::array<std::int64_t,6> otherArgs{};const void* opaqueArgs=nullptr;
void saveCall(unsigned command,std::array<std::int64_t,6> args,const void* pointer=nullptr){++otherCalls;otherCommand=command;otherArgs=args;opaqueArgs=pointer;SetLastError(2000+command);}
void STDMETHODCALLTYPE fakeIndexed(ID3D11DeviceContext*,UINT n,UINT start,INT base){saveCall(12,{n,start,base,0,0,0});}
void STDMETHODCALLTYPE fakeIndexedInstances(ID3D11DeviceContext*,UINT n,UINT instances,UINT start,INT base,UINT first){saveCall(20,{n,instances,start,base,first,0});}
void STDMETHODCALLTYPE fakeInstances(ID3D11DeviceContext*,UINT n,UINT instances,UINT start,UINT first){saveCall(21,{n,instances,start,first,0,0});}
void STDMETHODCALLTYPE fakeAuto(ID3D11DeviceContext*){saveCall(38,{});}
void STDMETHODCALLTYPE fakeIndexedIndirect(ID3D11DeviceContext*,ID3D11Buffer* buffer,UINT offset){saveCall(39,{offset,0,0,0,0,0},buffer);}
void STDMETHODCALLTYPE fakeIndirect(ID3D11DeviceContext*,ID3D11Buffer* buffer,UINT offset){saveCall(40,{offset,0,0,0,0,0},buffer);}
void STDMETHODCALLTYPE fakeExecute(ID3D11DeviceContext*,ID3D11CommandList* commands,BOOL restore){saveCall(58,{restore,0,0,0,0,0},commands);}
HRESULT STDMETHODCALLTYPE fakeQuery(ID3D11DeviceContext*,REFIID,void** out){*out=nullptr;return E_NOINTERFACE;}
ULONG STDMETHODCALLTYPE fakeRef(ID3D11DeviceContext*){return 1;}
void STDMETHODCALLTYPE noShader(ID3D11DeviceContext*,ID3D11PixelShader** out,ID3D11ClassInstance**,UINT*){*out=nullptr;SetLastError(999);}
ID3D11DeviceContext* fakeContext=nullptr;
void STDMETHODCALLTYPE getFakeContext(ID3D11Device*,ID3D11DeviceContext** out){*out=fakeContext;}
void dispatchControls(){
    static std::array<void*,116> storage{};auto* table=storage.data()+1;
    for(unsigned i=0;i<115;++i)table[i]=bits<void*>(&fakeRef);
    table[0]=bits<void*>(&fakeQuery);table[13]=bits<void*>(&fakeDraw);table[74]=bits<void*>(&noShader);
    table[12]=bits<void*>(&fakeIndexed);table[20]=bits<void*>(&fakeIndexedInstances);table[21]=bits<void*>(&fakeInstances);
    table[38]=bits<void*>(&fakeAuto);table[39]=bits<void*>(&fakeIndexedIndirect);table[40]=bits<void*>(&fakeIndirect);table[58]=bits<void*>(&fakeExecute);
    static struct {void** table;} object{table};fakeContext=reinterpret_cast<ID3D11DeviceContext*>(&object);
    static std::array<void*,43> deviceTable{};deviceTable[40]=bits<void*>(&getFakeContext);
    static struct {void** table;} device{deviceTable.data()};
    auto* observer=new Observer(Launch{});Observer::instance.store(observer);observer->attachContext(reinterpret_cast<ID3D11Device*>(&device));
    check(observer->status.state==5&&observer->contexts[0].count==115,"owned base-context ABI admission");
    const auto cached=bits<Draw>(observer->contexts[0].facade[13]);nativeDraws=0;
    fakeContext->Draw(17,9);check(nativeDraws==1&&lastCount==17&&lastStart==9&&GetLastError()==1234,"native arguments and LastError survive diagnostic getters exactly once");
    table[13]=bits<void*>(&nextDraw);fakeContext->Draw(19,7);
    check(nativeDraws==2&&lastCount==19&&lastStart==7&&GetLastError()==4321,"forwarding follows changed current native dispatch without a stale target");
    fakeContext->DrawIndexed(31,7,-4);check(otherCalls==1&&otherCommand==12&&otherArgs==std::array<std::int64_t,6>{31,7,-4,0,0,0}&&GetLastError()==2012,"indexed arguments and negative base are forwarded exactly once");
    fakeContext->DrawIndexedInstanced(27,3,5,-2,11);check(otherCalls==2&&otherCommand==20&&otherArgs==std::array<std::int64_t,6>{27,3,5,-2,11,0}&&GetLastError()==2020,"indexed-instance arguments retain every field");
    fakeContext->DrawInstanced(33,4,7,13);check(otherCalls==3&&otherCommand==21&&otherArgs==std::array<std::int64_t,6>{33,4,7,13,0,0}&&GetLastError()==2021,"instance arguments retain every field");
    fakeContext->DrawAuto();check(otherCalls==4&&otherCommand==38&&GetLastError()==2038,"auto draw forwards once");
    static int ownedMarker=0;auto* buffer=reinterpret_cast<ID3D11Buffer*>(&ownedMarker);
    fakeContext->DrawIndexedInstancedIndirect(buffer,28);check(otherCalls==5&&otherCommand==39&&otherArgs[0]==28&&opaqueArgs==buffer&&GetLastError()==2039,"indexed indirect forwards its opaque buffer and offset without reading it");
    fakeContext->DrawInstancedIndirect(buffer,44);check(otherCalls==6&&otherCommand==40&&otherArgs[0]==44&&opaqueArgs==buffer&&GetLastError()==2040,"indirect forwards its opaque buffer and offset without reading it");
    auto* commands=reinterpret_cast<ID3D11CommandList*>(&ownedMarker);fakeContext->ExecuteCommandList(commands,TRUE);
    check(otherCalls==7&&otherCommand==58&&otherArgs[0]==TRUE&&opaqueArgs==commands&&GetLastError()==2058&&!observer->serial,"command-list replay forwards once and never fabricates source samples");
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
float4 unreviewed(float4 p:SV_Position):SV_Target{return scene.Load(int3(int2(p.xy),0))*float4(0.5,0.75,0.25,1)+overlay.Load(int3(int2(p.xy),0))*0.125;}
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
    Com<ID3D11Buffer> indices;const std::uint32_t indexData[]{99,0,1,2};D3D11_BUFFER_DESC ib{};ib.ByteWidth=sizeof indexData;ib.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexInitial{indexData,sizeof indexData,0};check(SUCCEEDED(device.p->CreateBuffer(&ib,&indexInitial,indices.out())),"actual index buffer with an ignored prefix");
    context.p->IASetIndexBuffer(indices.p,DXGI_FORMAT_R32_UINT,0);
    Com<ID3D11Buffer> indirect,indexedIndirect;const std::uint32_t indirectData[]{99,98,97,96,3,2,0,7},indexedData[]{99,98,97,96,3,2,1,0,7};
    D3D11_BUFFER_DESC arg{};arg.ByteWidth=sizeof indirectData;arg.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
    D3D11_SUBRESOURCE_DATA initialArgs{indirectData,sizeof indirectData,0};check(SUCCEEDED(device.p->CreateBuffer(&arg,&initialArgs,indirect.out())),"owned indirect argument buffer");
    arg.ByteWidth=sizeof indexedData;initialArgs.pSysMem=indexedData;check(SUCCEEDED(device.p->CreateBuffer(&arg,&initialArgs,indexedIndirect.out())),"owned indexed indirect argument buffer");
    for(unsigned variant=0;variant<5;++variant){
        const auto issue=[&](bool native){
            if(variant==0){if(native)bits<decltype(&fakeIndexed)>(dispatch.function(12))(context.p,3,1,0);else context.p->DrawIndexed(3,1,0);}
            if(variant==1){if(native)bits<decltype(&fakeIndexedInstances)>(dispatch.function(20))(context.p,3,2,1,0,7);else context.p->DrawIndexedInstanced(3,2,1,0,7);}
            if(variant==2){if(native)bits<decltype(&fakeInstances)>(dispatch.function(21))(context.p,3,2,0,7);else context.p->DrawInstanced(3,2,0,7);}
            if(variant==3){if(native)bits<decltype(&fakeIndexedIndirect)>(dispatch.function(39))(context.p,indexedIndirect.p,16);else context.p->DrawIndexedInstancedIndirect(indexedIndirect.p,16);}
            if(variant==4){if(native)bits<decltype(&fakeIndirect)>(dispatch.function(40))(context.p,indirect.p,16);else context.p->DrawInstancedIndirect(indirect.p,16);}
        };
        context.p->ClearRenderTargetView(rtv.p,zero);issue(true);const auto expected=pixels(context.p,target.p,staging.p);
        context.p->ClearRenderTargetView(rtv.p,zero);const auto before=observer->draws.load();issue(false);
        check(observer->draws==before+1&&dispatch.owns(),"every actual raster family is observed exactly once");
        check(expected==pixels(context.p,target.p,staging.p),"every indexed/instanced/indirect family retains all original RGBA bytes");
    }
    Com<ID3DBlob> unknownCode;Com<ID3D11PixelShader> unknown;
    check(SUCCEEDED(D3DCompile(Program,sizeof Program-1,nullptr,nullptr,nullptr,"unreviewed","ps_5_0",0,0,unknownCode.out(),nullptr))&&
        SUCCEEDED(device.p->CreatePixelShader(unknownCode.p->GetBufferPointer(),unknownCode.p->GetBufferSize(),nullptr,unknown.out())),"owned unreviewed pixel program");
    NativeShaderDescription description{};check(observer->shaders.identify(unknown.p)==NativeShader::Unknown&&observer->shaders.describe(unknown.p,description)&&description.pixel&&description.kind==NativeShader::Unknown,"unknown fingerprint metadata never recognizes pixel semantics");
    std::array<std::uint8_t,32> expectedHash{};check(dxbcFingerprint(unknownCode.p->GetBufferPointer(),unknownCode.p->GetBufferSize(),expectedHash)&&description.hash==expectedHash,"unknown digest belongs to its actual created program");
    context.p->PSSetShader(unknown.p,nullptr,0);context.p->ClearRenderTargetView(rtv.p,zero);bits<Draw>(dispatch.function(13))(context.p,3,0);
    const auto unknownBaseline=pixels(context.p,target.p,staging.p);const auto beforeSerial=observer->serial;const auto reviewedBefore=observer->recognized.load();
    context.p->ClearRenderTargetView(rtv.p,zero);context.p->Draw(3,0);
    check(unknownBaseline==pixels(context.p,target.p,staging.p)&&observer->recognized==reviewedBefore&&observer->serial==beforeSerial+2,"unknown pixels with a reviewed vertex program yield only scalar bindings, unchanged RGB and no semantic recognition");
    const auto& unknownSample=observer->status.samples[(observer->serial-1)%SourceAuditSamples];
    check(!unknownSample.shader&&unknownSample.vertexKind==unsigned(NativeShader::StreamVertex)&&unknownSample.programHashValid&&unknownSample.programHash==expectedHash,"unknown observations stay explicitly unknown and retain their exact fingerprint");
    context.p->VSSetShader(nullptr,nullptr,0);const auto noVertex=observer->serial;context.p->Draw(3,0);
    check(observer->serial==noVertex,"unreviewed pixels without a reviewed vertex never enter the bounded source observation");
    context.p->VSSetShader(vs.p,nullptr,0);
    Com<ID3D11ClassLinkage> linkage;Com<ID3D11PixelShader> linked;
    check(SUCCEEDED(device.p->CreateClassLinkage(linkage.out()))&&SUCCEEDED(device.p->CreatePixelShader(psCode.p->GetBufferPointer(),psCode.p->GetBufferSize(),linkage.p,linked.out())),"actual linked pixel creation with reviewed code bytes");
    check(observer->shaders.identify(linked.p)==NativeShader::Unknown&&observer->shaders.describe(linked.p,description)&&description.linked&&description.pixel&&description.kind==NativeShader::Unknown,"linked pixel metadata remains Unknown even when its code hash is reviewed");
    context.p->PSSetShader(linked.p,nullptr,0);context.p->ClearRenderTargetView(rtv.p,zero);bits<Draw>(dispatch.function(13))(context.p,3,0);
    const auto linkedBaseline=pixels(context.p,target.p,staging.p);const auto linkedReviewed=observer->recognized.load();context.p->ClearRenderTargetView(rtv.p,zero);context.p->Draw(3,0);
    check(linkedBaseline==pixels(context.p,target.p,staging.p)&&observer->recognized==linkedReviewed,"linked semantics are never promoted and native output remains unchanged");
    const auto& linkedSample=observer->status.samples[(observer->serial-1)%SourceAuditSamples];check(!linkedSample.shader&&linkedSample.programLinked&&linkedSample.programHashValid,"linked binding observations explicitly retain their unresolved linkage");
    context.p->PSSetShader(ps.p,nullptr,0);
    const auto beforeContention=observer->draws.load();
    // Contention must drop diagnostics instead of waiting on a render thread.
    const auto drops=observer->dropped.load();std::atomic<bool> locked{false},done{false};
    std::thread cold([&]{std::lock_guard<std::mutex> guard(observer->mutex);locked=true;while(!done)SwitchToThread();});
    while(!locked)SwitchToThread();
    context.p->Draw(3,0);done=true;cold.join();
    check(observer->dropped==drops+1&&observer->draws==beforeContention+1,"cold diagnostic contention never blocks or duplicates native drawing");
    Com<ID3D11Device> foreign;Com<ID3D11DeviceContext> foreignContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,foreign.out(),nullptr,foreignContext.out())),"independent unobserved native device");
    const auto observations=observer->recognized.load();foreignContext.p->Draw(0,0);
    check(observer->recognized==observations&&observer->draws==beforeContention+1,"other contexts sharing the native table only forward");
    observer->stopped=true;observer->detach();observer->shaders.stop();
    check(*reinterpret_cast<void***>(context.p)==originalTable,"stop restores the exact owned object vptr without changing its native table");
    const auto serial=observer->serial,beforeCached=observer->draws.load();context.p->ClearRenderTargetView(rtv.p,zero);cached(context.p,3,0);
    check(observer->draws==beforeCached+1&&observer->serial==serial,"cached callback remains a native-only forwarder after stop");
    check(baseline==pixels(context.p,target.p,staging.p),"stop preserves all native RGBA output bytes");

}
}
int main(){try{dispatchControls();run();std::printf("source audit owned WARP: %u checks PASS\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"source audit failed after %u: %s\n",checks,e.what());return 1;}}
