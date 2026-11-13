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
const std::array<ShaderFingerprint,3> Contracts{{
    {NativeShader::StreamVertex,shaderDigest("5f33e43e2f388b7d2b0baa2fe89d9fd64062c9781c622ea6183bc67af70fc0fa")},
    {NativeShader::StreamCombine,shaderDigest("c359fa168f4e80e54395bf0587de803139839cf79f204e783384b5b3a9865e28")},
    {NativeShader::StreamInset,shaderDigest("d9f935dcb136936ad3f61f79629ba20e1dd1db861d62490800bd051a67a01442")}
}};
// Metadata-only same-caller observer. It never draws, copies a GPU image,
// changes thread protection, blocks a graphics thread or grants source output.
struct Observer {
    Launch launch;Page* page=nullptr;HANDLE mapping=nullptr,parent=nullptr;
    NativeShaders shaders;
    std::mutex mutex;SourceAuditInventory inventory{};SourceAuditStatus status{};
    struct Table {void** address=nullptr;void* original=nullptr;void* wrapper=nullptr;bool attached=false;};
    std::array<Table,4> tables{};
    std::array<std::atomic<ID3D11DeviceContext*>,4> contexts{};
    std::atomic<bool> stopped{false};std::atomic<std::uint64_t> draws{0},recognized{0},dropped{0};
    std::uint64_t serial=0;
    static std::atomic<Observer*> instance;
    explicit Observer(const Launch& value,const ShaderFingerprint* contracts=Contracts.data(),std::size_t count=Contracts.size()):
        launch(value),shaders(contracts,count,deviceObserved,this,true){}
    static bool writable(void** slot,DWORD& protection)noexcept {
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(slot,&memory,sizeof memory)||memory.State!=MEM_COMMIT||
            memory.Protect&(PAGE_GUARD|PAGE_NOACCESS|PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))return false;
        const auto offset=reinterpret_cast<std::uintptr_t>(slot)-reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if(offset>memory.RegionSize||sizeof(void*)>memory.RegionSize-offset)return false;
        return VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protection)!=0;
    }
    void detach()noexcept {
        for(auto& table:tables){DWORD old=0;
            if(table.attached&&writable(table.address+13,old)){
                InterlockedCompareExchangePointer(table.address+13,table.original,table.wrapper);
                DWORD ignored=0;(void)VirtualProtect(table.address+13,sizeof(void*),old,&ignored);table.attached=false;
            }
        }
    }
    static void deviceObserved(ID3D11Device* device,void* opaque)noexcept {
        auto& s=*static_cast<Observer*>(opaque);
        try{s.attachContext(device);}catch(...){std::lock_guard<std::mutex> guard(s.mutex);s.status.state=8;s.status.error=ERROR_UNHANDLED_EXCEPTION;}
    }
    void attachContext(ID3D11Device* device){
        std::lock_guard<std::mutex> guard(mutex);if(stopped)return;
        Held<ID3D11DeviceContext> context;device->GetImmediateContext(context.out());
        if(!context.p){status.state=8;status.error=ERROR_INVALID_HANDLE;return;}
        unsigned free=0;
        for(;free<contexts.size();++free){const auto old=contexts[free].load(std::memory_order_acquire);
            if(old==context.p)return;
            if(!old)break;}
        if(free==contexts.size()){status.state=8;status.error=ERROR_TOO_MANY_NAMES;return;}
        void** address=nullptr;void* originalDraw=nullptr;
        if(!readable(context.p,&address,sizeof address)||!address||!readable(address+13,&originalDraw,sizeof originalDraw)||!originalDraw){
            status.state=8;status.error=ERROR_NOACCESS;return;
        }
        // Executed synchronously before the native factory publishes this
        // device/context. No cold thread calls a SINGLETHREADED D3D interface.
        // Retain the complete native vptr, private tail and RTTI. A shared table
        // may serve other contexts; those calls forward without observation.
        Table* table=nullptr;
        for(auto& candidate:tables)if(candidate.address==address)table=&candidate;
        if(table&&originalDraw!=table->wrapper){status.state=8;status.error=ERROR_BUSY;return;}
        if(!table){unsigned index=0;for(;index<tables.size()&&tables[index].address;++index){}
            if(index==tables.size()){status.state=8;status.error=ERROR_TOO_MANY_NAMES;return;}
            DWORD old=0;
            if(!writable(address+13,old)){status.state=8;status.error=ERROR_NOACCESS;return;}
            const std::array<void*,4> wrappers{{bits<void*>(&draw<0>),bits<void*>(&draw<1>),bits<void*>(&draw<2>),bits<void*>(&draw<3>)}};
            table=&tables[index];table->address=address;table->original=originalDraw;table->wrapper=wrappers[index];
            const bool attached=InterlockedCompareExchangePointer(address+13,table->wrapper,originalDraw)==originalDraw;
            table->attached=attached;
            DWORD ignored=0;const bool protectedAgain=VirtualProtect(address+13,sizeof(void*),old,&ignored)!=0;
            if(!attached||!protectedAgain){detach();status.state=8;status.error=ERROR_BUSY;return;}
        }
        contexts[free].store(context.p,std::memory_order_release);context.p=nullptr;
        status.state=5;
    }
    template<unsigned Index>static void STDMETHODCALLTYPE draw(ID3D11DeviceContext* ctx,UINT count,UINT start){
        auto& s=*instance.load(std::memory_order_acquire);
        bits<void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT)>(s.tables[Index].original)(ctx,count,start);
        struct PreserveError{DWORD value=GetLastError();~PreserveError(){SetLastError(value);}} error;
        ++s.draws;if(s.stopped)return;
        bool observed=false;for(auto& known:s.contexts)if(known.load(std::memory_order_acquire)==ctx){observed=true;break;}
        if(!observed)return;
        try{s.inspect(ctx);}catch(...){++s.dropped;}
    }
    void inspect(ID3D11DeviceContext* ctx){
        Held<ID3D11PixelShader> shader;ctx->PSGetShader(shader.out(),nullptr,nullptr);
        const auto kind=shaders.identify(shader.p);
        if(kind!=NativeShader::StreamCombine&&kind!=NativeShader::StreamInset)return;
        ++recognized;
        std::unique_lock<std::mutex> guard(mutex,std::try_to_lock);if(!guard){++dropped;return;}
        for(unsigned slot=0;slot<(kind==NativeShader::StreamCombine?2u:1u);++slot){
            Held<ID3D11ShaderResourceView> view;ctx->PSGetShaderResources(slot,1,view.out());if(!view.p)continue;
            Held<ID3D11Resource> resource;view.p->GetResource(resource.out());if(!resource.p)continue;
            Held<ID3D11Texture2D> texture;
            if(FAILED(resource.p->QueryInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(texture.out()))))continue;
            D3D11_TEXTURE2D_DESC shape{};texture.p->GetDesc(&shape);
            Held<IDXGIResource> shared;HANDLE handle=nullptr;
            if(SUCCEEDED(resource.p->QueryInterface(IID_IDXGIResource,reinterpret_cast<void**>(shared.out()))))
                (void)shared.p->GetSharedHandle(&handle);
            auto sample=auditSource(inventory,reinterpret_cast<std::uintptr_t>(handle),shape.Width,shape.Height,unsigned(shape.Format),shape.SampleDesc.Count,shape.ArraySize);
            sample.shader=unsigned(kind);sample.slot=slot;
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};view.p->GetDesc(&vd);sample.view=unsigned(vd.ViewDimension);sample.viewFormat=unsigned(vd.Format);
            Held<ID3D11RenderTargetView> out;ctx->OMGetRenderTargets(1,out.out(),nullptr);
            if(out.p){Held<ID3D11Resource> output;out.p->GetResource(output.out());Held<ID3D11Texture2D> target;
                if(output.p&&SUCCEEDED(output.p->QueryInterface(IID_ID3D11Texture2D,reinterpret_cast<void**>(target.out())))){
                    D3D11_TEXTURE2D_DESC d{};target.p->GetDesc(&d);sample.outputWidth=d.Width;sample.outputHeight=d.Height;sample.outputFormat=unsigned(d.Format);
                }
            }
            const auto key=[](const SourceAuditSample& p){return std::make_tuple(p.inventoryEpoch,p.allocationGeneration,
                p.shader,p.slot,p.pid,p.width,p.height,p.format,p.arraySize,p.samples,p.view,p.viewFormat,p.ownerMatches,
                p.outputWidth,p.outputHeight,p.outputFormat);};
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
