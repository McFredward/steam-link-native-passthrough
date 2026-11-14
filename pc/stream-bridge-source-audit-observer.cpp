#include "stream-bridge-source-audit-windows.h"
#include "stream-bridge-source-audit-shared.h"
#include "stream-bridge-sender-provider-shaders.h"
#include "stream-bridge-native-image.h"
#include <d3d11_4.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace stream_bridge::sender::source_audit {
namespace {
template<class To,class From>To bits(From value)noexcept{static_assert(sizeof(To)==sizeof(From),"Win64 pointer ABI");To out{};std::memcpy(&out,&value,sizeof out);return out;}
template<class T>void release(T*& p)noexcept{if(p)p->Release();p=nullptr;}
template<class T>struct Held{T* p=nullptr;~Held(){release(p);}T** out()noexcept{return &p;}};
bool readable(const void* from,void* to,std::size_t bytes)noexcept{SIZE_T count=0;return ReadProcessMemory(GetCurrentProcess(),from,to,bytes,&count)&&count==bytes;}
const std::array<ShaderFingerprint,19> Contracts{{
    {NativeShader::StreamVertex,shaderDigest("5f33e43e2f388b7d2b0baa2fe89d9fd64062c9781c622ea6183bc67af70fc0fa")},
    {NativeShader::StreamCombine,shaderDigest("c359fa168f4e80e54395bf0587de803139839cf79f204e783384b5b3a9865e28")},
    {NativeShader::StreamInset,shaderDigest("d9f935dcb136936ad3f61f79629ba20e1dd1db861d62490800bd051a67a01442")},
    {NativeShader::MultiViewInner,shaderDigest("f8dc71b5055fca7daeccf1e9fe62b804b1ea12bb1f5be758656bdcde5d44eb50")},
    {NativeShader::MultiViewDownscaleX,shaderDigest("458eb4a8e711033adbc3e010f046014b42a9b70da5e40d32a088de370f26b853")},
    {NativeShader::MultiViewDownscaleY,shaderDigest("19d23135a8b403887043c106fb6aa998ad666a483959a9ee97a1d76bca744e69")},
    {NativeShader::MultiViewReprojectX,shaderDigest("c74f8931dbadf4c791c38cbb3290baa1db666ea23c33193b304718dd9f3a6c7e")},
    {NativeShader::MultiViewReprojectY,shaderDigest("675da9edd13d0f410d2f60e83446a483a653e1cdea0fcae1607791715c868a72")},
    {NativeShader::MultiViewResolve,shaderDigest("c48e8ffff836b55f565dcc0001bbf5f4d5ff3e77498b249a16d6c7a327bda4be")},
    {NativeShader::MultiViewVertexX,shaderDigest("d6ae45c3e426a9ccfe41e60e0de3c4d8f7f30db3891c42576e78cd0e1d84170c")},
    {NativeShader::MultiViewVertexY,shaderDigest("a6fb3a02dba65a6a14ad212d731569e5949482b3a3e19f515d81dc54fb880be5")},
    // Additional observed program families remain diagnostic labels. These
    // records cannot authorize game/eye/frame ancestry or mask composition.
    {NativeShader::StreamCombine,shaderDigest("c9ca3256f589cb7d1b79cd9682ad26d834b9b9ffaf969dd9c95575aa5cdf0bfd")},
    {NativeShader::StreamInset,shaderDigest("e6b6b25064e11ec0874d173c894c84d3b46c3815dba7b9ce5c8882e975d0e854")},
    {NativeShader::MultiViewInner,shaderDigest("eb43d281e767a94ff2865b8f06720372b1f32365f1174298895910b6fe63f26c")},
    {NativeShader::MultiViewDownscaleY,shaderDigest("63faa7c4c75298a8c4f270d55a925c12af972cc23b557c823c2ba49b47de544e")},
    {NativeShader::MultiViewReprojectY,shaderDigest("f81e7fc43b88c44c609d4cb478e61984c1ca0a0d28b29ad645a04a01b6ccc838")},
    {NativeShader::MultiViewResolve,shaderDigest("cd176ca332ec4239c61e27f099a38c90983cc75e25d8b96042c3f4e0f040a72a")},
    {NativeShader::MultiViewVertexX,shaderDigest("099307078b6adec9586fb239e252ba31e30e2119fa22759a08a7d619481174d2")},
    {NativeShader::MultiViewVertexY,shaderDigest("187a168a6ad16a7889b51623cb0006959ecae7c519c2bf3c46c06e11cb05dd00")}
}};
// Metadata-only same-caller observer. It never draws, copies a GPU image,
// changes thread protection, blocks a graphics thread or grants source output.
struct Observer {
    Launch launch;Page* page=nullptr;HANDLE mapping=nullptr,parent=nullptr;
    NativeShaders shaders;
    std::mutex mutex;SourceAuditInventory inventory{};SourceAuditStatus status{};
    struct Context {
        ID3D11DeviceContext* object=nullptr;void** original=nullptr;void** facade=nullptr;
        std::array<void*,149> expected{};unsigned count=0;void* allocation=nullptr;
        bool owns()const noexcept {
            void** current=nullptr;std::array<void*,149> observed{};
            return object&&facade&&readable(object,&current,sizeof current)&&current==facade&&
                readable(facade,observed.data(),count*sizeof(void*))&&std::memcmp(observed.data(),expected.data(),count*sizeof(void*))==0;
        }
        std::uintptr_t function(unsigned slot)const noexcept {
            return original&&slot<count?reinterpret_cast<const volatile std::uintptr_t*>(original)[slot]:0;
        }
    };
    std::array<Context,4> contexts{};
    std::array<std::atomic<ID3D11DeviceContext*>,4> activeContexts{};
    std::atomic<bool> stopped{false};std::atomic<std::uint64_t> draws{0},recognized{0},dropped{0};
    std::array<std::atomic<std::uint64_t>,8> rasterCalls{};
    std::uint64_t serial=0;
    static std::atomic<Observer*> instance;
    explicit Observer(const Launch& value,const ShaderFingerprint* contracts=Contracts.data(),std::size_t count=Contracts.size()):
        launch(value),shaders(contracts,count,deviceObserved,this,true,true){}
    void detach()noexcept {
        for(auto& context:contexts)if(context.owns())
            InterlockedCompareExchangePointer(reinterpret_cast<void**>(context.object),context.original,context.facade);
    }
    static void deviceObserved(ID3D11Device* device,void* opaque)noexcept {
        auto& s=*static_cast<Observer*>(opaque);
        try{s.attachContext(device);}catch(...){std::lock_guard<std::mutex> guard(s.mutex);s.status.state=8;s.status.error=ERROR_UNHANDLED_EXCEPTION;}
    }
    void attachContext(ID3D11Device* device){
        std::lock_guard<std::mutex> guard(mutex);if(stopped)return;
        Held<ID3D11DeviceContext> native;device->GetImmediateContext(native.out());
        if(!native.p){status.state=8;status.error=ERROR_INVALID_HANDLE;return;}
        unsigned index=0;
        for(;index<contexts.size();++index){if(contexts[index].object==native.p)return;if(!contexts[index].object)break;}
        if(index==contexts.size()){status.state=8;status.error=ERROR_TOO_MANY_NAMES;return;}
        auto& ctx=contexts[index];ctx.object=native.p;native.p=nullptr;ctx.count=115;
        const std::array<GUID,4> versions{{__uuidof(ID3D11DeviceContext4),__uuidof(ID3D11DeviceContext3),__uuidof(ID3D11DeviceContext2),__uuidof(ID3D11DeviceContext1)}};
        const unsigned lengths[4]{149,147,144,134};
        for(unsigned i=0;i<versions.size();++i){void* alias=nullptr;
            if(SUCCEEDED(ctx.object->QueryInterface(versions[i],&alias))){
                const bool same=alias==ctx.object;if(alias)static_cast<IUnknown*>(alias)->Release();
                if(!same){status.state=8;status.error=ERROR_NOT_SUPPORTED;return;}ctx.count=lengths[i];break;
            }
        }
        std::uintptr_t prefix=0;
        if(!readable(ctx.object,&ctx.original,sizeof ctx.original)||!ctx.original||
            !readable(ctx.original,ctx.expected.data(),ctx.count*sizeof(void*))||!readable(ctx.original-1,&prefix,sizeof prefix)){
            status.state=8;status.error=ERROR_NOACCESS;return;
        }
        for(unsigned i=0;i<ctx.count;++i)if(!ctx.expected[i]){status.state=8;status.error=ERROR_INVALID_DATA;return;}
        SYSTEM_INFO system{};GetSystemInfo(&system);const auto bytes=std::size_t(system.dwPageSize);
        if(bytes<ctx.count*16||bytes<(ctx.count+1)*sizeof(void*)){status.state=8;status.error=ERROR_NOT_SUPPORTED;return;}
        ctx.allocation=VirtualAlloc(nullptr,2*bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(!ctx.allocation){status.state=8;status.error=GetLastError();return;}
        ctx.facade=static_cast<void**>(ctx.allocation)+1;ctx.facade[-1]=reinterpret_cast<void*>(prefix);
        auto* code=static_cast<unsigned char*>(ctx.allocation)+bytes;
        for(unsigned i=0;i<ctx.count;++i){const unsigned char thunk[16]{0xf3,0x0f,0x1e,0xfa,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0x20};
            std::memcpy(code+i*16,thunk,16);const auto address=reinterpret_cast<std::uintptr_t>(ctx.original+i);
            std::memcpy(code+i*16+6,&address,8);ctx.expected[i]=code+i*16;
        }
#define SFNP_AUDIT_ATTACH(Slot,Name) {const std::array<void*,4> callbacks{{bits<void*>(&Name<0>),bits<void*>(&Name<1>),bits<void*>(&Name<2>),bits<void*>(&Name<3>)}};ctx.expected[Slot]=callbacks[index];}
        SFNP_AUDIT_ATTACH(12,drawIndexed) SFNP_AUDIT_ATTACH(13,draw)
        SFNP_AUDIT_ATTACH(20,drawIndexedInstanced) SFNP_AUDIT_ATTACH(21,drawInstanced)
        SFNP_AUDIT_ATTACH(38,drawAuto) SFNP_AUDIT_ATTACH(39,drawIndexedIndirect)
        SFNP_AUDIT_ATTACH(40,drawIndirect) SFNP_AUDIT_ATTACH(58,execute)
#undef SFNP_AUDIT_ATTACH
        std::memcpy(ctx.facade,ctx.expected.data(),ctx.count*sizeof(void*));DWORD old=0;
        if(!VirtualProtect(code,bytes,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code,bytes)){
            status.state=8;status.error=GetLastError();return;
        }
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(ctx.object,&memory,sizeof memory)||memory.State!=MEM_COMMIT||memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)){
            status.state=8;status.error=ERROR_NOACCESS;return;
        }
        const auto access=memory.Protect&0xffu;
        if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY){status.state=8;status.error=ERROR_NOACCESS;return;}
        // Same-caller metadata policy: no multithread-protection changes or cold
        // D3D calls. Like the sender facade, forwarding follows each CURRENT
        // native method cell after runtime first-use dispatch specialization.
        activeContexts[index].store(ctx.object,std::memory_order_release);
        if(InterlockedCompareExchangePointer(reinterpret_cast<void**>(ctx.object),ctx.facade,ctx.original)!=ctx.original){
            activeContexts[index].store(nullptr,std::memory_order_release);status.state=8;status.error=ERROR_BUSY;return;
        }
        status.state=5;
    }
    struct Raster {
        std::uint32_t command=0,count=0,instances=0,start=0,firstInstance=0,argumentOffset=0;
        std::int32_t baseVertex=0;
    };
    template<unsigned Index>void afterRaster(ID3D11DeviceContext* ctx,unsigned counter,const Raster& call)noexcept{
        ++draws;++rasterCalls[counter];if(stopped||ctx!=activeContexts[Index].load(std::memory_order_acquire))return;
        try{inspect(ctx,call);}catch(...){++dropped;}
    }
#define SFNP_AUDIT_RASTER(Name,Slot,Counter,Parameters,Arguments,Call) \
    template<unsigned Index>static void STDMETHODCALLTYPE Name Parameters { \
        auto& s=*instance.load(std::memory_order_acquire); \
        bits<decltype(&Name<Index>)>(s.contexts[Index].function(Slot)) Arguments; \
        struct PreserveError{DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} error; \
        s.afterRaster<Index>(ctx,Counter,Call); \
    }
    SFNP_AUDIT_RASTER(drawIndexed,12,0,(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base),(ctx,count,start,base),(Raster{12,count,1,start,0,0,base}))
    SFNP_AUDIT_RASTER(draw,13,1,(ID3D11DeviceContext* ctx,UINT count,UINT start),(ctx,count,start),(Raster{13,count,1,start,0,0,0}))
    SFNP_AUDIT_RASTER(drawIndexedInstanced,20,2,(ID3D11DeviceContext* ctx,UINT count,UINT instances,UINT start,INT base,UINT first),(ctx,count,instances,start,base,first),(Raster{20,count,instances,start,first,0,base}))
    SFNP_AUDIT_RASTER(drawInstanced,21,3,(ID3D11DeviceContext* ctx,UINT count,UINT instances,UINT start,UINT first),(ctx,count,instances,start,first),(Raster{21,count,instances,start,first,0,0}))
    SFNP_AUDIT_RASTER(drawAuto,38,4,(ID3D11DeviceContext* ctx),(ctx),(Raster{38,0,0,0,0,0,0}))
    SFNP_AUDIT_RASTER(drawIndexedIndirect,39,5,(ID3D11DeviceContext* ctx,ID3D11Buffer* args,UINT offset),(ctx,args,offset),(Raster{39,0,0,0,0,offset,0}))
    SFNP_AUDIT_RASTER(drawIndirect,40,6,(ID3D11DeviceContext* ctx,ID3D11Buffer* args,UINT offset),(ctx,args,offset),(Raster{40,0,0,0,0,offset,0}))
#undef SFNP_AUDIT_RASTER
    template<unsigned Index>static void STDMETHODCALLTYPE execute(ID3D11DeviceContext* ctx,ID3D11CommandList* commands,BOOL restore){
        auto& s=*instance.load(std::memory_order_acquire);
        bits<decltype(&execute<Index>)>(s.contexts[Index].function(58))(ctx,commands,restore);
        struct PreserveError{DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} error;
        // Current bindings after a replay do not prove the command list's
        // sources. Count this boundary without assigning it image ancestry.
        ++s.rasterCalls[7];
    }
    void inspect(ID3D11DeviceContext* ctx,const Raster& call){
        Held<ID3D11PixelShader> shader;ctx->PSGetShader(shader.out(),nullptr,nullptr);if(!shader.p)return;
        const auto kind=shaders.identify(shader.p);
        const bool multi=kind>=NativeShader::MultiViewInner&&kind<=NativeShader::MultiViewResolve;
        const bool reviewed=kind==NativeShader::StreamCombine||kind==NativeShader::StreamInset||multi;
        Held<ID3D11VertexShader> vertex;ctx->VSGetShader(vertex.out(),nullptr,nullptr);
        if(!vertex.p)return;
        const auto vertexKind=shaders.identify(vertex.p);
        const bool programCandidate=reviewed||vertexKind==NativeShader::StreamVertex||
            vertexKind==NativeShader::MultiViewVertexX||vertexKind==NativeShader::MultiViewVertexY;
        if(reviewed)++recognized;
        NativeShaderDescription program{};const bool hashValid=shaders.describe(shader.p,program)&&program.pixel;

        std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);if(!guard){++dropped;return;}
        const unsigned sourceSlots=kind==NativeShader::MultiViewReprojectX?3u:
            (kind==NativeShader::StreamInset||multi?1u:2u);
        for(unsigned slot=0;slot<sourceSlots;++slot){
            Held<ID3D11ShaderResourceView> view;ctx->PSGetShaderResources(slot,1,view.out());if(!view.p)continue;
            Held<ID3D11Resource> resource;view.p->GetResource(resource.out());if(!resource.p)continue;
            Held<ID3D11Texture2D> texture;
            if(FAILED(resource.p->QueryInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(texture.out()))))continue;
            D3D11_TEXTURE2D_DESC shape{};texture.p->GetDesc(&shape);
            Held<IDXGIResource> shared;HANDLE handle=nullptr;
            if(SUCCEEDED(resource.p->QueryInterface(IID_IDXGIResource,reinterpret_cast<void**>(shared.out()))))
                (void)shared.p->GetSharedHandle(&handle);
            auto sample=auditSource(inventory,reinterpret_cast<std::uintptr_t>(handle),shape.Width,shape.Height,unsigned(shape.Format),shape.SampleDesc.Count,shape.ArraySize);
            // A registered non-compositor allocation can also expose earlier
            // scene draws with other vertex programs. This is only a bounded
            // ancestry diagnostic, never a source/eye/frame permission.
            if(!programCandidate&&(sample.ownerMatches!=1||!sample.pid||sample.pid==status.pid))continue;
            sample.shader=unsigned(kind);sample.slot=slot;sample.vertexKind=unsigned(vertexKind);
            sample.command=call.command;sample.count=call.count;sample.instances=call.instances;sample.start=call.start;
            sample.firstInstance=call.firstInstance;sample.argumentOffset=call.argumentOffset;sample.baseVertex=call.baseVertex;
            sample.programHashValid=hashValid;sample.programHash=program.hash;sample.programLinked=program.linked;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view.p->GetDesc(&vd);sample.view=unsigned(vd.ViewDimension);sample.viewFormat=unsigned(vd.Format);
            Held<ID3D11RenderTargetView> out;ctx->OMGetRenderTargets(1,out.out(),nullptr);
            if(out.p){Held<ID3D11Resource> output;out.p->GetResource(output.out());Held<ID3D11Texture2D> target;
                if(output.p&&SUCCEEDED(output.p->QueryInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(target.out())))){
                    D3D11_TEXTURE2D_DESC d{};target.p->GetDesc(&d);sample.outputWidth=d.Width;sample.outputHeight=d.Height;sample.outputFormat=unsigned(d.Format);
                }
            }
            const auto key=[](const SourceAuditSample& p){return std::make_tuple(p.inventoryEpoch,p.allocationGeneration,
                p.shader,p.slot,p.pid,p.width,p.height,p.format,p.arraySize,p.samples,p.view,p.viewFormat,p.ownerMatches,
                p.outputWidth,p.outputHeight,p.outputFormat,p.command,p.count,p.instances,p.start,p.firstInstance,p.argumentOffset,p.baseVertex,p.vertexKind,p.programHashValid,p.programHash,p.programLinked);};
            bool duplicate=false;
            for(const auto& previous:status.samples)if(previous.serial&&key(previous)==key(sample)){duplicate=true;break;}
            if(duplicate)continue;
            if(serial==UINT64_MAX){++dropped;continue;}
            sample.serial=++serial;status.samples[(serial-1)%SourceAuditSamples]=sample;
            status.sampleCount=static_cast<std::uint32_t>(std::min<std::uint64_t>(SourceAuditSamples,serial));status.state=6;
        }
    }
    bool prepare(){
        wchar_t exe[32768]{};const auto n=GetModuleFileNameW(nullptr,exe,32768);
        const auto* slash=n?std::wcsrchr(exe,L'\\'):nullptr;
        if(!n||n>=32768||!slash||_wcsicmp(slash+1,L"vrcompositor.exe"))return false;
        parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,launch.server);
        if(!parent||birth(parent)!=launch.serverBirth)return false;
        wchar_t key[96]{};if(!name(launch.server,key))return false;
        mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,key);if(!mapping)return false;
        page=static_cast<Page*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Page)));
        if(!page||page->magic!=Magic||page->server!=launch.server||page->bytes!=sizeof(Page))return false;
        status.pid=GetCurrentProcessId();status.state=3;
        // Inspect only the current executable's file to resolve a unique public
        // D3D11 factory import. No private object offsets or native entry point
        // address allowlists are used, and no foreign process memory is read.
        HANDLE file=CreateFileW(exe,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool okay=GetFileSizeEx(file,&size)&&size.QuadPart>=512&&size.QuadPart<=64*1024*1024;
        std::vector<std::uint8_t> raw(okay?static_cast<std::size_t>(size.QuadPart):0);DWORD used=0;
        if(okay)okay=ReadFile(file,raw.data(),static_cast<DWORD>(raw.size()),&used,nullptr)&&used==raw.size();
        CloseHandle(file);
        NativePeImage image;std::uint32_t slot=0;
        if(!okay||!image.open(raw.data(),raw.size())||!image.deviceFactoryImport(slot))return false;
        HMODULE retained=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&SFNP_CompositorSourceAuditStartV1),&retained))return false;
        status.state=4;
        return shaders.arm(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))+slot);
    }
    void publishStatus(){
        std::lock_guard<std::mutex> guard(mutex);SourceAuditInventory fresh{};
        if(read(page->inventorySequence,page->inventory,fresh))inventory=fresh;
        const auto stats=shaders.stats();status.devices=stats.devices;status.shaders=stats.recognized;
        status.draws=draws.load();status.recognizedDraws=recognized.load();status.dropped=dropped.load();
        status.streamShaderKinds=stats.streamKinds;status.unknownPixelPrograms=stats.unknownPixels;
        status.unknownPixelOverflow=stats.unknownPixelOverflow;
        status.multiViewShaderKinds=stats.multiViewKinds;
        status.factoryCalls=stats.factoryCalls;status.factoryCreated=stats.factoryCreated;status.factoryRefused=stats.factoryRefused;
        for(unsigned i=0;i<rasterCalls.size();++i)status.rasterCalls[i]=rasterCalls[i].load();
        publish(page->statusSequence,page->status,status);
    }
    void run()noexcept{
        bool ready=false;
        try{
            ready=prepare();
            if(ready){while(WaitForSingleObject(parent,250)==WAIT_TIMEOUT&&!InterlockedCompareExchange(&page->stopped,0,0))publishStatus();}
            else{std::lock_guard<std::mutex> guard(mutex);status.pid=GetCurrentProcessId();status.state=8;status.error=GetLastError();}
        }catch(...){std::lock_guard<std::mutex> guard(mutex);status.state=8;status.error=ERROR_UNHANDLED_EXCEPTION;}
        stopped=true;
        // Fetched callbacks and their originals remain retained until process
        // exit. Never reinstall over a foreign owner.
        {std::lock_guard<std::mutex> guard(mutex);
            detach();
            shaders.stop();if(ready)status.state=7;if(page)publish(page->statusSequence,page->status,status);
        }
        if(page){UnmapViewOfFile(page);page=nullptr;}
        if(mapping){CloseHandle(mapping);mapping=nullptr;}
        if(parent){CloseHandle(parent);parent=nullptr;}
    }
};
std::atomic<Observer*> Observer::instance{nullptr};
}
} // namespace stream_bridge::sender::source_audit
extern "C" __declspec(dllexport) DWORD WINAPI SFNP_CompositorSourceAuditStartV1(void* argument)noexcept{
    using namespace stream_bridge::sender::source_audit;
    Launch launch{};
    if(!readable(argument,&launch,sizeof launch)||launch.magic!=Magic||!launch.server||!launch.serverBirth||launch.reserved)return 0;
    try{auto* observer=new Observer(launch);Observer* expected=nullptr;
        if(!Observer::instance.compare_exchange_strong(expected,observer)){delete observer;return 0;}
        try{std::thread([observer]{observer->run();}).detach();return 1;}
        catch(...){Observer::instance.store(nullptr);delete observer;return 0;}
    }catch(...){return 0;}
}
