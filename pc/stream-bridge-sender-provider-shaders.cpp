#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "stream-bridge-sender-provider-shaders.h"
#include <d3d11_4.h>
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
    struct Device {
        std::atomic<ID3D11Device*> object{nullptr};void** original=nullptr;void** facade=nullptr;
        void* allocation=nullptr;unsigned count=0;
        std::array<void*,69> expected{};
    };
    struct Shader {std::atomic<IUnknown*> object{nullptr};NativeShader kind=NativeShader::Unknown;};
    std::array<Device,4> devices{};std::array<Shader,64> shaders{};
    std::array<ShaderFingerprint,32> contracts{};std::size_t contractCount=0;
    std::mutex mutex;std::atomic<bool> stopped{false},published{false};
    std::atomic<std::uint64_t> deviceCount{0},recognized{0},refused{0};
    void** import=nullptr;DeviceFactory originalFactory=nullptr;
    static std::atomic<Impl*> instance;
    Device* find(ID3D11Device* object)noexcept{for(auto& slot:devices)if(slot.object.load(std::memory_order_acquire)==object)return &slot;return nullptr;}
    void retain(ID3D11DeviceChild* shader,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage)noexcept{
        if(stopped||!shader||linkage)return;
        const auto kind=classifyNativeShader(code,size,contracts.data(),contractCount);
        if(kind==NativeShader::Unknown){++refused;return;}
        IUnknown* identity=nullptr;if(FAILED(shader->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))||!identity)return;
        std::lock_guard<std::mutex> guard(mutex);
        for(const auto& old:shaders)if(old.object.load(std::memory_order_acquire)==identity){identity->Release();return;}
        for(auto& slot:shaders)if(!slot.object.load(std::memory_order_relaxed)){
            slot.kind=kind;slot.object.store(identity,std::memory_order_release);++recognized;return;
        }
        identity->Release();++refused;
    }
    bool observe(ID3D11Device* object)noexcept{
        if(!object||stopped||object->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)return false;
        Impl* expected=nullptr;if(!instance.compare_exchange_strong(expected,this)&&expected!=this)return false;
        std::lock_guard<std::mutex> guard(mutex);if(find(object))return true;
        Device* slot=nullptr;for(auto& entry:devices)if(!entry.object.load()){slot=&entry;break;}
        if(!slot){++refused;return false;}
        const std::array<GUID,5> versions{{__uuidof(ID3D11Device5),__uuidof(ID3D11Device4),__uuidof(ID3D11Device3),__uuidof(ID3D11Device2),__uuidof(ID3D11Device1)}};
        const unsigned lengths[]{69,67,65,54,50};unsigned count=43;
        for(unsigned i=0;i<versions.size();++i){void* alias=nullptr;
            if(SUCCEEDED(object->QueryInterface(versions[i],&alias))){
                if(!alias)return false;
                const bool same=alias==object;static_cast<IUnknown*>(alias)->Release();
                if(!same)return false;
                count=lengths[i];break;
            }
        }
        void** original=nullptr;std::array<void*,69> entries{};
        if(!shaderRead(object,&original,sizeof original)||!original||!shaderRead(original,entries.data(),count*sizeof(void*)))return false;
        for(unsigned i=0;i<count;++i)if(!entries[i])return false;
        SYSTEM_INFO info{};GetSystemInfo(&info);const auto page=std::size_t(info.dwPageSize);
        if(page<count*16)return false;
        void* memory=VirtualAlloc(nullptr,2*page,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!memory)return false;
        auto** facade=static_cast<void**>(memory);auto* code=static_cast<std::uint8_t*>(memory)+page;
        for(unsigned i=0;i<count;++i){
            const std::uint8_t thunk[]{0xf3,0x0f,0x1e,0xfa,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0x20};
            std::memcpy(code+16*i,thunk,sizeof thunk);const auto source=reinterpret_cast<std::uintptr_t>(original+i);
            std::memcpy(code+16*i+6,&source,sizeof source);slot->expected[i]=code+16*i;
        }
        slot->expected[12]=shaderBits<void*>(&createVertex);slot->expected[15]=shaderBits<void*>(&createPixel);
        std::memcpy(facade,slot->expected.data(),count*sizeof(void*));DWORD old=0;
        if(!VirtualProtect(code,page,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),code,page)){VirtualFree(memory,0,MEM_RELEASE);return false;}
        slot->original=original;slot->facade=facade;slot->allocation=memory;slot->count=count;
        object->AddRef();slot->object.store(object,std::memory_order_release);
        if(InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),facade,original)!=original){
            slot->object.store(nullptr,std::memory_order_release);object->Release();VirtualFree(memory,0,MEM_RELEASE);slot->allocation=nullptr;return false;
        }
        published=true;++deviceCount;return true;
    }
    static HRESULT STDMETHODCALLTYPE createVertex(ID3D11Device* object,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11VertexShader** output){
        auto& self=*instance.load(std::memory_order_acquire);auto* slot=self.find(object);if(!slot)return E_UNEXPECTED;
        const auto original=shaderBits<CreateVertex>(reinterpret_cast<const volatile std::uintptr_t*>(slot->original)[12]);
        const auto result=original(object,code,size,linkage,output);const auto error=GetLastError();
        if(SUCCEEDED(result)&&output&&*output)self.retain(*output,code,size,linkage);
        SetLastError(error);return result;
    }
    static HRESULT STDMETHODCALLTYPE createPixel(ID3D11Device* object,const void* code,SIZE_T size,ID3D11ClassLinkage* linkage,ID3D11PixelShader** output){
        auto& self=*instance.load(std::memory_order_acquire);auto* slot=self.find(object);if(!slot)return E_UNEXPECTED;
        const auto original=shaderBits<CreatePixel>(reinterpret_cast<const volatile std::uintptr_t*>(slot->original)[15]);
        const auto result=original(object,code,size,linkage,output);const auto error=GetLastError();
        if(SUCCEEDED(result)&&output&&*output)self.retain(*output,code,size,linkage);
        SetLastError(error);return result;
    }
    static HRESULT WINAPI createDevice(IDXGIAdapter* adapter,D3D_DRIVER_TYPE type,HMODULE software,UINT flags,const D3D_FEATURE_LEVEL* levels,UINT count,UINT sdk,ID3D11Device** device,D3D_FEATURE_LEVEL* level,ID3D11DeviceContext** context){
        auto& self=*instance.load(std::memory_order_acquire);
        const auto result=self.originalFactory(adapter,type,software,flags,levels,count,sdk,device,level,context);const auto error=GetLastError();
        if(SUCCEEDED(result)&&device&&*device)(void)self.observe(*device);
        SetLastError(error);return result;
    }
};
std::atomic<NativeShaders::Impl*> NativeShaders::Impl::instance{nullptr};
NativeShaders::NativeShaders(const ShaderFingerprint* contracts,std::size_t count):impl_(new Impl){
    if(contracts&&count<=impl_->contracts.size()){std::copy_n(contracts,count,impl_->contracts.begin());impl_->contractCount=count;}
}
NativeShaders::~NativeShaders(){stop();if(impl_->published)(void)impl_.release();}
bool NativeShaders::arm(std::uintptr_t address)noexcept{
    auto& self=*impl_;if(!address||self.import||self.stopped)return false;
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
    self.import=slot;self.published=true;if(!restored){stop();return false;}return true;
}
bool NativeShaders::observeDevice(ID3D11Device* device)noexcept{return impl_->observe(device);}
NativeShader NativeShaders::identify(ID3D11DeviceChild* shader)const noexcept{
    if(!shader||impl_->stopped)return NativeShader::Unknown;
    IUnknown* identity=nullptr;
    if(FAILED(shader->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&identity)))||!identity)return NativeShader::Unknown;
    NativeShader result=NativeShader::Unknown;
    for(const auto& slot:impl_->shaders)if(slot.object.load(std::memory_order_acquire)==identity){result=slot.kind;break;}
    identity->Release();return result;
}
NativeShaderStats NativeShaders::stats()const noexcept{return {impl_->deviceCount.load(),impl_->recognized.load(),impl_->refused.load(),impl_->import&&!impl_->stopped};}
void NativeShaders::stop()noexcept{
    auto& self=*impl_;self.stopped=true;std::lock_guard<std::mutex> guard(self.mutex);
    if(self.import){DWORD old=0;if(VirtualProtect(self.import,sizeof(void*),PAGE_READWRITE,&old)){
        InterlockedCompareExchangePointer(self.import,shaderBits<void*>(self.originalFactory),shaderBits<void*>(&Impl::createDevice));DWORD ignored=0;VirtualProtect(self.import,sizeof(void*),old,&ignored);
    }}
    for(auto& slot:self.devices)if(auto* object=slot.object.load(std::memory_order_acquire)){
        void** table=nullptr;std::array<void*,69> observed{};
        if(shaderRead(object,&table,sizeof table)&&table==slot.facade&&shaderRead(table,observed.data(),slot.count*sizeof(void*))&&!std::memcmp(observed.data(),slot.expected.data(),slot.count*sizeof(void*)))
            InterlockedCompareExchangePointer(reinterpret_cast<void**>(object),slot.original,slot.facade);
    }
}
} // namespace stream_bridge::sender
