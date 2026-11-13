#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "stream-bridge-sender-provider-shaders.h"
#include <d3d11.h>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <cstring>
namespace stream_bridge::sender {
namespace {
template<class To,class From>To shaderBits(From from)noexcept{static_assert(sizeof(To)==sizeof(From),"public Win64 pointer");To to{};std::memcpy(&to,&from,sizeof to);return to;}
using DeviceFactory=HRESULT(WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
using CreateVertex=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
using CreatePixel=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
bool shaderRead(const void* from,void* to,std::size_t size)noexcept{SIZE_T used=0;return ReadProcessMemory(GetCurrentProcess(),from,to,size,&used)&&used==size;}
}
struct NativeShaders::Impl {
    struct Table {
        void** address=nullptr;CreateVertex vertex=nullptr;CreatePixel pixel=nullptr;
        void* wrappedVertex=nullptr;void* wrappedPixel=nullptr;bool published=false,attached=false;
    };
    struct Device {std::atomic<ID3D11Device*> object{nullptr};Table* methods=nullptr;std::atomic<bool> active{false};};
    struct Shader {std::atomic<IUnknown*> object{nullptr};NativeShader kind=NativeShader::Unknown;std::array<std::uint8_t,32> hash{};bool pixel=false;};
    std::array<Table,4> tables{};std::array<Device,4> devices{};std::array<Shader,64> shaders{};
    std::array<Shader,128> unknownPixels{};
    std::array<ShaderFingerprint,32> contracts{};std::size_t contractCount=0;
    std::mutex mutex;std::atomic<bool> stopped{false},published{false};
    DeviceObserved observer=nullptr;void* observerOpaque=nullptr;bool readOnlySingleThreaded=false;
    bool rememberUnknownPixels=false;
    std::atomic<std::uint64_t> deviceCount{0},recognized{0},refused{0};
    void** import=nullptr;DeviceFactory originalFactory=nullptr;
    static std::atomic<Impl*> instance;
    Device* find(ID3D11Device* object)noexcept{for(auto& slot:devices)if(slot.object.load(std::memory_order_acquire)==object)return &slot;return nullptr;}
    void retain(ID3D11Device* device,ID3D11DeviceChild* shader,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,bool pixel)noexcept{
        auto* owner=find(device);if(stopped||!owner||!owner->active.load(std::memory_order_acquire)||!shader||linkage)return;
        const auto kind=classifyNativeShader(code,size,contracts.data(),contractCount);
        if(kind==NativeShader::Unknown){++refused;if(!rememberUnknownPixels||!pixel)return;}
        std::array<std::uint8_t,32> hash{};if(!dxbcFingerprint(code,size,hash))return;
        IUnknown* identity=nullptr;if(FAILED(shader->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))||!identity)return;
        std::lock_guard<std::mutex> guard(mutex);
        if(stopped){identity->Release();return;}
        const auto keep=[&](auto& entries){
            for(const auto& old:entries)if(old.object.load(std::memory_order_acquire)==identity){identity->Release();return true;}
            for(auto& slot:entries)if(!slot.object.load(std::memory_order_relaxed)){
                slot.kind=kind;slot.hash=hash;slot.pixel=pixel;slot.object.store(identity,std::memory_order_release);
                if(kind!=NativeShader::Unknown)++recognized;
                return true;
            }
            return false;
        };
        if(kind==NativeShader::Unknown){if(keep(unknownPixels))return;}
        else if(keep(shaders))return;
        identity->Release();if(kind!=NativeShader::Unknown)++refused;
    }
    template<unsigned Index>static HRESULT STDMETHODCALLTYPE createVertex(ID3D11Device* object,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11VertexShader** output){
        auto& self=*instance.load(std::memory_order_acquire);
        const auto result=self.tables[Index].vertex(object,code,size,linkage,output);const auto error=GetLastError();
        if(SUCCEEDED(result)&&output&&*output)self.retain(object,*output,code,size,linkage,false);
        SetLastError(error);return result;
    }
    template<unsigned Index>static HRESULT STDMETHODCALLTYPE createPixel(ID3D11Device* object,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** output){
        auto& self=*instance.load(std::memory_order_acquire);
        const auto result=self.tables[Index].pixel(object,code,size,linkage,output);const auto error=GetLastError();
        if(SUCCEEDED(result)&&output&&*output)self.retain(object,*output,code,size,linkage,true);
        SetLastError(error);return result;
    }
    static bool writable(void** address,DWORD& old)noexcept{
        MEMORY_BASIC_INFORMATION memory{};const auto first=reinterpret_cast<std::uintptr_t>(address+12);
        if(!VirtualQuery(address+12,&memory,sizeof memory)||memory.State!=MEM_COMMIT||
           memory.Protect&(PAGE_GUARD|PAGE_NOACCESS|PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))return false;
        const auto base=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if(first<base||first-base>memory.RegionSize||4*sizeof(void*)>memory.RegionSize-(first-base))return false;
        return VirtualProtect(address+12,4*sizeof(void*),PAGE_READWRITE,&old)!=0;
    }
    bool observe(ID3D11Device* object)noexcept{
        if(!object||stopped||(!readOnlySingleThreaded&&(object->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)))return false;
        Impl* expected=nullptr;if(!instance.compare_exchange_strong(expected,this)&&expected!=this)return false;
        std::lock_guard<std::mutex> guard(mutex);if(stopped)return false;
        if(auto* old=find(object))return old->active.load(std::memory_order_acquire);
        Device* device=nullptr;for(auto& entry:devices)if(!entry.object.load()){device=&entry;break;}
        if(!device){++refused;return false;}
        void** address=nullptr;std::array<void*,16> entries{};
        if(!shaderRead(object,&address,sizeof address)||!address||!shaderRead(address,entries.data(),sizeof entries)||!entries[12]||!entries[15])return false;
        Table* table=nullptr;for(auto& candidate:tables)if(candidate.address==address)table=&candidate;
        if(table&&((entries[12]!=table->wrappedVertex&&entries[12]!=shaderBits<void*>(table->vertex))||
                   (entries[15]!=table->wrappedPixel&&entries[15]!=shaderBits<void*>(table->pixel))))return false;
        if(!table||entries[12]!=table->wrappedVertex||entries[15]!=table->wrappedPixel){
            unsigned index=0;
            if(table)index=static_cast<unsigned>(table-tables.data());
            else{for(;index<tables.size()&&tables[index].address;++index){}if(index==tables.size()){++refused;return false;}}
            // Admission precedes ownership: a refused executable/guarded page
            // must never be changed by observation or by subsequent cleanup.
            DWORD old=0;if(!writable(address,old))return false;
            if(!table){
                table=&tables[index];table->address=address;table->vertex=shaderBits<CreateVertex>(entries[12]);table->pixel=shaderBits<CreatePixel>(entries[15]);
                const std::array<void*,4> vertices{{shaderBits<void*>(&createVertex<0>),shaderBits<void*>(&createVertex<1>),shaderBits<void*>(&createVertex<2>),shaderBits<void*>(&createVertex<3>)}};
                const std::array<void*,4> pixels{{shaderBits<void*>(&createPixel<0>),shaderBits<void*>(&createPixel<1>),shaderBits<void*>(&createPixel<2>),shaderBits<void*>(&createPixel<3>)}};
                table->wrappedVertex=vertices[index];table->wrappedPixel=pixels[index];
            }
            // Preserve the complete native vptr/private tail/RTTI. Each shared
            // table has its own originals; unobserved devices only forward.
            const bool vertex=InterlockedCompareExchangePointer(address+12,table->wrappedVertex,entries[12])==entries[12];
            if(vertex){table->published=true;table->attached=true;published=true;}
            const bool pixel=vertex&&InterlockedCompareExchangePointer(address+15,table->wrappedPixel,entries[15])==entries[15];
            DWORD ignored=0;const bool protectedAgain=VirtualProtect(address+12,4*sizeof(void*),old,&ignored)!=0;
            if(!pixel||!protectedAgain){
                DWORD undo=0;if(table->attached&&writable(address,undo)){
                    InterlockedCompareExchangePointer(address+12,shaderBits<void*>(table->vertex),table->wrappedVertex);
                    InterlockedCompareExchangePointer(address+15,shaderBits<void*>(table->pixel),table->wrappedPixel);
                    table->attached=false;VirtualProtect(address+12,4*sizeof(void*),old,&ignored);
                }
                // Published callbacks may have been cached: retain their slot
                // and originals, but permit a later retry after exact rollback.
                if(!table->published)*table=Table{};
                return false;
            }
        }
        object->AddRef();device->methods=table;device->active=true;device->object.store(object,std::memory_order_release);++deviceCount;return true;
    }
    static HRESULT WINAPI createDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
        auto& self=*instance.load(std::memory_order_acquire);
        const auto result=self.originalFactory(adapter,type,software,flags,levels,count,sdk,device,level,context);const auto error=GetLastError();
        if(SUCCEEDED(result)&&device&&*device&&self.observe(*device)&&self.observer)
            self.observer(*device,self.observerOpaque);
        SetLastError(error);return result;
    }
};
std::atomic<NativeShaders::Impl*> NativeShaders::Impl::instance{nullptr};
NativeShaders::NativeShaders(const ShaderFingerprint* contracts,std::size_t count,
    DeviceObserved observer,void* opaque,bool readOnlySingleThreaded,bool rememberUnknownPixels):impl_(new Impl){
    if(contracts&&count<=impl_->contracts.size()){std::copy_n(contracts,count,impl_->contracts.begin());impl_->contractCount=count;}
    impl_->observer=observer;impl_->observerOpaque=opaque;impl_->readOnlySingleThreaded=readOnlySingleThreaded;
    impl_->rememberUnknownPixels=rememberUnknownPixels;
}
NativeShaders::~NativeShaders(){stop();if(impl_->published)(void)impl_.release();}
bool NativeShaders::arm(std::uintptr_t address)noexcept{
    auto& self=*impl_;if(!address)return false;
    std::unique_lock<std::mutex> guard(self.mutex);if(self.import||self.stopped)return false;
    const auto module=GetModuleHandleW(L"d3d11.dll");if(!module)return false;
    const auto original=GetProcAddress(module,"D3D11CreateDevice");if(!original)return false;
    void* observed=nullptr;auto** slot=reinterpret_cast<void**>(address);
    if(!shaderRead(slot,&observed,sizeof observed)||observed!=shaderBits<void*>(original))return false;
    Impl* expected=nullptr;if(!Impl::instance.compare_exchange_strong(expected,&self)&&expected!=&self)return false;
    self.originalFactory=shaderBits<DeviceFactory>(original);DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    const auto previous=InterlockedCompareExchangePointer(slot,shaderBits<void*>(&Impl::createDevice),observed);
    DWORD ignored=0;const bool restored=VirtualProtect(slot,sizeof(void*),old,&ignored)!=0;
    if(previous!=observed)return false;
    self.import=slot;self.published=true;if(!restored){guard.unlock();stop();return false;}return true;
}
bool NativeShaders::observeDevice(ID3D11Device* device)noexcept{
    const bool observed=impl_->observe(device);
    if(observed&&impl_->observer)impl_->observer(device,impl_->observerOpaque);
    return observed;
}
NativeShader NativeShaders::identify(ID3D11DeviceChild* shader)const noexcept{
    if(!shader||impl_->stopped)return NativeShader::Unknown;
    IUnknown* identity=nullptr;
    if(FAILED(shader->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))||!identity)return NativeShader::Unknown;
    NativeShader result=NativeShader::Unknown;
    for(const auto& slot:impl_->shaders)if(slot.object.load(std::memory_order_acquire)==identity){result=slot.kind;break;}
    identity->Release();return result;
}
bool NativeShaders::describe(ID3D11DeviceChild* shader,NativeShaderDescription& out)const noexcept{
    out={};if(!shader||impl_->stopped)return false;
    IUnknown* identity=nullptr;if(FAILED(shader->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))||!identity)return false;
    const auto find=[&](const auto& entries){
        for(const auto& slot:entries)if(slot.object.load(std::memory_order_acquire)==identity){
            out.kind=slot.kind;out.hash=slot.hash;out.pixel=slot.pixel;return true;
        }
        return false;
    };
    const bool found=find(impl_->shaders)||find(impl_->unknownPixels);identity->Release();return found;
}
NativeShaderStats NativeShaders::stats()const noexcept{
    NativeShaderStats out{impl_->deviceCount.load(),impl_->recognized.load(),impl_->refused.load(),impl_->import&&!impl_->stopped};
    for(const auto& slot:impl_->shaders)if(slot.object.load(std::memory_order_acquire)){
        if(slot.kind==NativeShader::StreamVertex)++out.streamKinds[0];
        if(slot.kind==NativeShader::StreamCombine)++out.streamKinds[1];
        if(slot.kind==NativeShader::StreamInset)++out.streamKinds[2];
    }
    for(const auto& slot:impl_->unknownPixels)if(slot.object.load(std::memory_order_acquire))++out.unknownPixels;
    return out;
}
void NativeShaders::stop()noexcept{
    auto& self=*impl_;self.stopped=true;std::lock_guard<std::mutex> guard(self.mutex);
    if(self.import){DWORD old=0;if(VirtualProtect(self.import,sizeof(void*),PAGE_READWRITE,&old)){
        InterlockedCompareExchangePointer(self.import,shaderBits<void*>(self.originalFactory),shaderBits<void*>(&Impl::createDevice));DWORD ignored=0;VirtualProtect(self.import,sizeof(void*),old,&ignored);
    }}
    for(auto& table:self.tables)if(table.published&&table.attached){DWORD old=0;
        if(Impl::writable(table.address,old)){
            InterlockedCompareExchangePointer(table.address+12,shaderBits<void*>(table.vertex),table.wrappedVertex);
            InterlockedCompareExchangePointer(table.address+15,shaderBits<void*>(table.pixel),table.wrappedPixel);
            table.attached=false;DWORD ignored=0;VirtualProtect(table.address+12,4*sizeof(void*),old,&ignored);
        }
    }
}
} // namespace stream_bridge::sender
