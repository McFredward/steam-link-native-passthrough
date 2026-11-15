#pragma once
#include "stream-bridge-projected-alpha-sync.h"

namespace stream_bridge::sender::projected_alpha {
enum class TextureOrigin {Created,Opened};
// Cold native texture callbacks prepare bounded companion storage BEFORE a
// texture is returned to native rendering. Only public CreateTexture2D,
// OpenSharedResource and optional RTV/buffer/input-layout creation cells are
// wrapped. Buffer bytes come only from public initial-data/Map operations.
// Full native vptr/RTTI/private tails,
// descriptions, initial data, requested interfaces and results are preserved.
class TextureObserver {
public:
    using Observed=void(*)(ID3D11Device*,ID3D11Texture2D*,TextureOrigin,void*)noexcept;
    using TargetObserved=void(*)(ID3D11Device*,ID3D11RenderTargetView*,void*)noexcept;
    using BufferObserved=void(*)(ID3D11Device*,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer*,void*)noexcept;
    using LayoutObserved=void(*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout*,void*)noexcept;
private:
    using Create=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    using Open=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,HANDLE,REFIID,void**);
    using Target=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,ID3D11Resource*,const D3D11_RENDER_TARGET_VIEW_DESC*,ID3D11RenderTargetView**);
    using Buffer=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_BUFFER_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Buffer**);
    using Layout=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_INPUT_ELEMENT_DESC*,UINT,const void*,SIZE_T,ID3D11InputLayout**);
    template<class To,class From>static To bits(From value)noexcept{
        static_assert(sizeof(To)==sizeof(From),"Win64 pointer ABI");To out{};std::memcpy(&out,&value,sizeof out);return out;
    }
    struct Core {
        struct Table {void** address=nullptr;Create create=nullptr;Open open=nullptr;Target target=nullptr;
            Buffer buffer=nullptr;Layout layout=nullptr;
            void* wrappedCreate=nullptr;void* wrappedOpen=nullptr;void* wrappedTarget=nullptr;
            void* wrappedBuffer=nullptr;void* wrappedLayout=nullptr;};
        std::array<Table,4> tables{};
        std::array<std::atomic<ID3D11Device*>,4> devices{};
        std::array<std::atomic<Table*>,4> owners{};
        std::atomic<bool> stopped{false};bool published=false;
        Observed callback=nullptr;TargetObserved targetCallback=nullptr;void* opaque=nullptr;
        BufferObserved bufferCallback=nullptr;LayoutObserved layoutCallback=nullptr;
        inline static std::atomic<Core*> instance{nullptr};
        inline static thread_local unsigned companionDepth=0;
        static bool owned(const Table& table)noexcept{
            return table.address&&reinterpret_cast<void* volatile*>(table.address)[5]==table.wrappedCreate&&
                reinterpret_cast<void* volatile*>(table.address)[28]==table.wrappedOpen&&
                (!table.wrappedTarget||reinterpret_cast<void* volatile*>(table.address)[9]==table.wrappedTarget)&&
                (!table.wrappedBuffer||reinterpret_cast<void* volatile*>(table.address)[3]==table.wrappedBuffer)&&
                (!table.wrappedLayout||reinterpret_cast<void* volatile*>(table.address)[11]==table.wrappedLayout);
        }
        bool tracked(ID3D11Device* device)const noexcept{
            for(unsigned i=0;i<devices.size();++i)if(devices[i].load(std::memory_order_acquire)==device){
                const auto* table=owners[i].load(std::memory_order_acquire);return table&&owned(*table);
            }
            return false;
        }
        static bool writable(void** address,DWORD& previous)noexcept{
            MEMORY_BASIC_INFORMATION info{};const auto first=reinterpret_cast<std::uintptr_t>(address+3);
            if(!VirtualQuery(address+3,&info,sizeof info)||info.State!=MEM_COMMIT||
                info.Protect&(PAGE_GUARD|PAGE_NOACCESS|PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))return false;
            const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if(first<base||first-base>info.RegionSize||26*sizeof(void*)>info.RegionSize-(first-base))return false;
            return VirtualProtect(address+3,26*sizeof(void*),PAGE_READWRITE,&previous)!=FALSE;
        }
        void emit(ID3D11Device* device,ID3D11Texture2D* texture,TextureOrigin origin)noexcept{
            if(stopped||companionDepth||!callback||!texture||!tracked(device))return;
            struct Suppress {Suppress(){++companionDepth;}~Suppress(){--companionDepth;}} suppress;
            callback(device,texture,origin,opaque);
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE create(ID3D11Device* device,const D3D11_TEXTURE2D_DESC* desc,
            const D3D11_SUBRESOURCE_DATA* data,ID3D11Texture2D** texture){
            auto& s=*instance.load(std::memory_order_acquire);
            const auto result=s.tables[Index].create(device,desc,data,texture);const DWORD error=GetLastError();
            if(SUCCEEDED(result)&&texture&&*texture)s.emit(device,*texture,TextureOrigin::Created);
            SetLastError(error);return result;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE open(ID3D11Device* device,HANDLE handle,REFIID iid,void** output){
            auto& s=*instance.load(std::memory_order_acquire);
            const auto result=s.tables[Index].open(device,handle,iid,output);const DWORD error=GetLastError();
            if(SUCCEEDED(result)&&output&&*output&&!s.stopped&&!companionDepth&&s.tracked(device)){
                Reference<ID3D11Texture2D> texture;
                if(SUCCEEDED(static_cast<IUnknown*>(*output)->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))
                    s.emit(device,texture.value,TextureOrigin::Opened);
            }
            SetLastError(error);return result;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE target(ID3D11Device* device,ID3D11Resource* resource,
            const D3D11_RENDER_TARGET_VIEW_DESC* desc,ID3D11RenderTargetView** output){
            auto& s=*instance.load(std::memory_order_acquire);
            const auto result=s.tables[Index].target(device,resource,desc,output);const DWORD error=GetLastError();
            if(SUCCEEDED(result)&&output&&*output&&!s.stopped&&!companionDepth&&s.targetCallback&&s.tracked(device)){
                ++companionDepth;s.targetCallback(device,*output,s.opaque);--companionDepth;
            }
            SetLastError(error);return result;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE buffer(ID3D11Device* device,const D3D11_BUFFER_DESC* desc,
            const D3D11_SUBRESOURCE_DATA* data,ID3D11Buffer** output){
            auto& s=*instance.load(std::memory_order_acquire);
            const auto result=s.tables[Index].buffer(device,desc,data,output);const DWORD error=GetLastError();
            if(SUCCEEDED(result)&&output&&*output&&!s.stopped&&!companionDepth&&s.bufferCallback&&s.tracked(device)){
                ++companionDepth;s.bufferCallback(device,desc,data,*output,s.opaque);--companionDepth;
            }
            SetLastError(error);return result;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE layout(ID3D11Device* device,const D3D11_INPUT_ELEMENT_DESC* desc,
            UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout** output){
            auto& s=*instance.load(std::memory_order_acquire);
            const auto result=s.tables[Index].layout(device,desc,count,code,bytes,output);const DWORD error=GetLastError();
            if(SUCCEEDED(result)&&output&&*output&&!s.stopped&&!companionDepth&&s.layoutCallback&&s.tracked(device)){
                ++companionDepth;s.layoutCallback(device,desc,count,code,bytes,*output,s.opaque);--companionDepth;
            }
            SetLastError(error);return result;
        }
        // Like NativeShaders, cold device admission is externally serialized by
        // the owning controller. No mutex, allocation or I/O enters emit().
        bool prepare(ID3D11Device* device)noexcept{
            if(!device||stopped)return false;
            Core* expected=nullptr;if(!instance.compare_exchange_strong(expected,this)&&expected!=this)return false;
            if(tracked(device))return true;
            unsigned slot=0;while(slot<devices.size()&&devices[slot].load())++slot;if(slot==devices.size())return false;
            // Warm only owned tiny resources before retaining the current
            // method cells. Native first-use dispatch specialization must not
            // silently replace a just-installed observer on its first open.
            D3D11_TEXTURE2D_DESC probeDesc{};probeDesc.Width=probeDesc.Height=8;
            probeDesc.MipLevels=probeDesc.ArraySize=probeDesc.SampleDesc.Count=1;
            probeDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;probeDesc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            probeDesc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
            Reference<ID3D11Texture2D> probe,opened;Reference<IDXGIResource> shared;Reference<ID3D11RenderTargetView> view;
            HANDLE handle=nullptr;
            if(FAILED(device->CreateTexture2D(&probeDesc,nullptr,probe.out()))||
                FAILED(probe->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))||
                FAILED(shared->GetSharedHandle(&handle))||!handle||
                FAILED(device->OpenSharedResource(handle,__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(opened.out())))||
                (targetCallback&&FAILED(device->CreateRenderTargetView(opened.value,nullptr,view.out()))))return false;
            void** address=nullptr;SIZE_T read=0;std::array<void*,29> methods{};
            if(!ReadProcessMemory(GetCurrentProcess(),device,&address,sizeof address,&read)||read!=sizeof address||!address||
                !ReadProcessMemory(GetCurrentProcess(),address,methods.data(),sizeof methods,&read)||read!=sizeof methods||
                !methods[5]||!methods[28]||(targetCallback&&!methods[9])||
                (bufferCallback&&!methods[3])||(layoutCallback&&!methods[11]))return false;
            unsigned index=0;for(;index<tables.size();++index)if(tables[index].address==address||!tables[index].address)break;
            if(index==tables.size())return false;
            auto& table=tables[index];
            if(table.address){if(!owned(table))return false;}
            else{
                DWORD previous=0;if(!writable(address,previous))return false;
                table.address=address;table.create=bits<Create>(methods[5]);table.open=bits<Open>(methods[28]);table.target=bits<Target>(methods[9]);
                table.buffer=bits<Buffer>(methods[3]);table.layout=bits<Layout>(methods[11]);
                const std::array<void*,4> creates{{bits<void*>(&create<0>),bits<void*>(&create<1>),bits<void*>(&create<2>),bits<void*>(&create<3>)}};
                const std::array<void*,4> opens{{bits<void*>(&open<0>),bits<void*>(&open<1>),bits<void*>(&open<2>),bits<void*>(&open<3>)}};
                const std::array<void*,4> targets{{bits<void*>(&target<0>),bits<void*>(&target<1>),bits<void*>(&target<2>),bits<void*>(&target<3>)}};
                const std::array<void*,4> buffers{{bits<void*>(&buffer<0>),bits<void*>(&buffer<1>),bits<void*>(&buffer<2>),bits<void*>(&buffer<3>)}};
                const std::array<void*,4> layouts{{bits<void*>(&layout<0>),bits<void*>(&layout<1>),bits<void*>(&layout<2>),bits<void*>(&layout<3>)}};
                table.wrappedCreate=creates[index];table.wrappedOpen=opens[index];
                if(targetCallback)table.wrappedTarget=targets[index];
                if(bufferCallback)table.wrappedBuffer=buffers[index];
                if(layoutCallback)table.wrappedLayout=layouts[index];
                const bool first=InterlockedCompareExchangePointer(address+5,table.wrappedCreate,methods[5])==methods[5];
                if(first)published=true;
                const bool second=first&&InterlockedCompareExchangePointer(address+28,table.wrappedOpen,methods[28])==methods[28];
                const bool third=second&&(!table.wrappedTarget||InterlockedCompareExchangePointer(address+9,table.wrappedTarget,methods[9])==methods[9]);
                const bool fourth=third&&(!table.wrappedBuffer||InterlockedCompareExchangePointer(address+3,table.wrappedBuffer,methods[3])==methods[3]);
                const bool fifth=fourth&&(!table.wrappedLayout||InterlockedCompareExchangePointer(address+11,table.wrappedLayout,methods[11])==methods[11]);
                DWORD ignored=0;const bool restored=VirtualProtect(address+3,26*sizeof(void*),previous,&ignored)!=FALSE;
                if(!fifth||!restored){
                    DWORD undo=0;if(writable(address,undo)){
                        InterlockedCompareExchangePointer(address+5,methods[5],table.wrappedCreate);
                        InterlockedCompareExchangePointer(address+28,methods[28],table.wrappedOpen);
                        if(table.wrappedTarget)InterlockedCompareExchangePointer(address+9,methods[9],table.wrappedTarget);
                        if(table.wrappedBuffer)InterlockedCompareExchangePointer(address+3,methods[3],table.wrappedBuffer);
                        if(table.wrappedLayout)InterlockedCompareExchangePointer(address+11,methods[11],table.wrappedLayout);
                        VirtualProtect(address+3,26*sizeof(void*),previous,&ignored);
                    }
                    return false;
                }
            }
            device->AddRef();owners[slot].store(&table,std::memory_order_release);devices[slot].store(device,std::memory_order_release);return true;
        }
        void stop()noexcept{
            stopped=true;
            for(auto& table:tables)if(owned(table)){
                DWORD previous=0;if(!writable(table.address,previous))continue;
                InterlockedCompareExchangePointer(table.address+5,bits<void*>(table.create),table.wrappedCreate);
                InterlockedCompareExchangePointer(table.address+28,bits<void*>(table.open),table.wrappedOpen);
                if(table.wrappedTarget)InterlockedCompareExchangePointer(table.address+9,bits<void*>(table.target),table.wrappedTarget);
                if(table.wrappedBuffer)InterlockedCompareExchangePointer(table.address+3,bits<void*>(table.buffer),table.wrappedBuffer);
                if(table.wrappedLayout)InterlockedCompareExchangePointer(table.address+11,bits<void*>(table.layout),table.wrappedLayout);
                DWORD ignored=0;VirtualProtect(table.address+3,26*sizeof(void*),previous,&ignored);
            }
        }
    };
    Core* core_=new(std::nothrow) Core;
public:
    TextureObserver(Observed callback,void* opaque,TargetObserved target=nullptr,BufferObserved buffer=nullptr,LayoutObserved layout=nullptr)noexcept{
        if(core_){core_->callback=callback;core_->targetCallback=target;core_->bufferCallback=buffer;core_->layoutCallback=layout;core_->opaque=opaque;}
    }
    TextureObserver(const TextureObserver&)=delete;TextureObserver& operator=(const TextureObserver&)=delete;
    ~TextureObserver(){if(core_){core_->stop();if(!core_->published){Core* expected=core_;Core::instance.compare_exchange_strong(expected,nullptr);delete core_;}}}
    bool prepare(ID3D11Device* device)noexcept{return core_&&core_->prepare(device);}
    void stop()noexcept{if(core_)core_->stop();}
};
} // namespace stream_bridge::sender::projected_alpha
