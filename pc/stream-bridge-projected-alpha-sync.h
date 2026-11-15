#pragma once
#include "stream-bridge-projected-alpha-replay.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>

namespace stream_bridge::sender::projected_alpha {
struct MutexLease {
    std::uint64_t allocation=0,epoch=0,key=0;
    ID3D11Device* device=nullptr;
    IDXGIResource* retained=nullptr; // Borrowed only while the actual lease is held.
    explicit operator bool()const noexcept{return allocation&&epoch>1&&device;}
};
// Observe ORIGINAL native AcquireSync/ReleaseSync operations. Never acquires a
// native mutex itself or changes a native key, timeout, result or call count.
// The warm-up uses a distinct small owned probe, prepared on the device's
// owning thread. An epoch versions an observed lock; it is NOT a video frame.
class MutexObserver {
public:
    struct Diagnostics {
        std::uint64_t textures=0,shared=0,keyed=0,nonKeyed=0,tables=0,admissionRefused=0;
        std::uint64_t acquireCalls=0,acquireOkay=0,releaseCalls=0,identityRefused=0,slotRefused=0,active=0;
        unsigned identityReason=0;
    };
    struct Events {
        void (*acquired)(const MutexLease&,void*)noexcept=nullptr;
        void (*releasing)(const MutexLease&,std::uint64_t,void*)noexcept=nullptr;
        void (*released)(const MutexLease&,HRESULT,void*)noexcept=nullptr;
        void* opaque=nullptr;
    };
private:
    using Acquire=HRESULT(STDMETHODCALLTYPE*)(IDXGIKeyedMutex*,UINT64,DWORD);
    using Release=HRESULT(STDMETHODCALLTYPE*)(IDXGIKeyedMutex*,UINT64);
    template<class To,class From>static To bits(From value)noexcept{
        static_assert(sizeof(To)==sizeof(From),"Win64 pointer ABI");To out{};std::memcpy(&out,&value,sizeof out);return out;
    }
    struct Core {
        struct Table {void** address=nullptr;Acquire acquire=nullptr;Release release=nullptr;void* wrappedAcquire=nullptr;void* wrappedRelease=nullptr;};
        struct Slot {
            std::atomic<std::uint64_t> state{0},allocation{0},key{0};
            std::atomic<ID3D11Device*> device{nullptr};
            std::atomic<IDXGIResource*> held{nullptr};
        };
        std::array<Table,4> tables{};
        std::array<std::atomic<ID3D11Device*>,4> devices{};
        std::array<std::atomic<Table*>,4> deviceTables{};
        std::array<Slot,64> slots{};
        std::atomic<std::uint64_t> next{2},refused{0};
        std::atomic<std::uint64_t> textures{0},sharedTextures{0},keyedTextures{0},nonKeyedTextures{0},tableCount{0},admissionRefused{0};
        std::atomic<std::uint64_t> acquireCalls{0},acquireOkay{0},releaseCalls{0},identityRefused{0};
        std::atomic<unsigned> identityReason{0};
        std::atomic<bool> stopped{false};
        Events events{};bool published=false;
        inline static std::atomic<Core*> instance{nullptr};
        inline static thread_local unsigned nativeDepth=0,probeDepth=0;
        struct NativeCall {bool outer;NativeCall()noexcept:outer(nativeDepth++==0&&!probeDepth){}~NativeCall(){--nativeDepth;}};
        static bool writable(void** table,DWORD& previous)noexcept{
            MEMORY_BASIC_INFORMATION info{};const auto first=reinterpret_cast<std::uintptr_t>(table+8);
            if(!VirtualQuery(table+8,&info,sizeof info)||info.State!=MEM_COMMIT||
                info.Protect&(PAGE_GUARD|PAGE_NOACCESS|PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))return false;
            const auto base=reinterpret_cast<std::uintptr_t>(info.BaseAddress);
            if(first<base||first-base>info.RegionSize||2*sizeof(void*)>info.RegionSize-(first-base))return false;
            return VirtualProtect(table+8,2*sizeof(void*),PAGE_READWRITE,&previous)!=FALSE;
        }
        static bool owned(const Table& table)noexcept{
            return table.address&&reinterpret_cast<void* volatile*>(table.address)[8]==table.wrappedAcquire&&
                reinterpret_cast<void* volatile*>(table.address)[9]==table.wrappedRelease;
        }
        bool tracked(ID3D11Device* device)const noexcept{
            for(unsigned i=0;i<devices.size();++i)if(devices[i].load(std::memory_order_acquire)==device){
                const auto* table=deviceTables[i].load(std::memory_order_acquire);
                return table&&owned(*table);
            }
            return false;
        }
        bool identity(IDXGIKeyedMutex* mutex,MutexLease& lease,Reference<IDXGIResource>& hold)noexcept{
            const auto refuse=[this](unsigned reason)noexcept{identityReason=reason;identityRefused.fetch_add(1,std::memory_order_relaxed);return false;};
            Reference<ID3D11Device> device;Reference<ID3D11Texture2D> texture;
            if(!mutex)return refuse(1);
            // The DXGI interface need not expose an ID3D11Texture2D wrapper.
            // Its public resource and device identify the same retained kernel
            // allocation; preserve the previous texture route as a fallback.
            if(SUCCEEDED(mutex->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(hold.out()))))
                (void)hold->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(device.out()));
            if(!hold.value||!device.value){
                if(FAILED(mutex->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return refuse(1);
                if(!device.value)texture->GetDevice(device.out());
                if(!hold.value&&FAILED(texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(hold.out()))))return refuse(3);
            }
            if(!tracked(device.value))return refuse(2);
            HANDLE handle=nullptr;
            if(FAILED(hold->GetSharedHandle(&handle))||!handle||handle==INVALID_HANDLE_VALUE)return refuse(4);
            identityReason=0;
            lease.device=device.value;lease.retained=hold.value;
            lease.allocation=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));return true;
        }
        MutexLease current(ID3D11Device* device,std::uint64_t allocation)const noexcept{
            if(stopped||!device||!allocation||!tracked(device))return {};
            for(const auto& slot:slots){
                const auto before=slot.state.load(std::memory_order_acquire);
                if(before<2||slot.device.load(std::memory_order_relaxed)!=device||slot.allocation.load(std::memory_order_relaxed)!=allocation)continue;
                const MutexLease value{allocation,before,slot.key.load(std::memory_order_relaxed),device,slot.held.load(std::memory_order_relaxed)};
                if(slot.state.load(std::memory_order_acquire)==before)return value;
            }
            return {};
        }
        unsigned capture(ID3D11Device* device,std::array<MutexLease,8>& output)const noexcept{
            output={};if(stopped||!tracked(device))return 0;
            unsigned count=0;
            for(const auto& slot:slots){
                const auto epoch=slot.state.load(std::memory_order_acquire);
                if(epoch<2||slot.device.load(std::memory_order_relaxed)!=device)continue;
                const MutexLease value{slot.allocation.load(std::memory_order_relaxed),epoch,
                    slot.key.load(std::memory_order_relaxed),device,slot.held.load(std::memory_order_relaxed)};
                if(slot.state.load(std::memory_order_acquire)!=epoch||!value.retained)return 0;
                if(count==output.size()){output={};return 0;}
                output[count++]=value;
            }
            return count;
        }
        void acquired(IDXGIKeyedMutex* object,std::uint64_t key)noexcept{
            if(stopped)return;
            MutexLease value{};Reference<IDXGIResource> hold;if(!identity(object,value,hold))return;
            for(auto& slot:slots){
                std::uint64_t empty=0;
                if(!slot.state.compare_exchange_strong(empty,1,std::memory_order_acq_rel))continue;
                const auto epoch=next.fetch_add(1,std::memory_order_relaxed);
                if(epoch<2||epoch==std::numeric_limits<std::uint64_t>::max()){
                    stopped=true;slot.state.store(0,std::memory_order_release);return;
                }
                value.key=key;value.epoch=epoch;
                slot.device.store(value.device,std::memory_order_relaxed);slot.allocation.store(value.allocation,std::memory_order_relaxed);
                slot.key.store(key,std::memory_order_relaxed);slot.held.store(hold.value,std::memory_order_relaxed);hold.value=nullptr;
                slot.state.store(epoch,std::memory_order_release);
                if(events.acquired)events.acquired(value,events.opaque);
                return;
            }
            ++refused;
        }
        Slot* releasing(IDXGIKeyedMutex* object,std::uint64_t key,MutexLease& value)noexcept{
            Reference<IDXGIResource> hold;if(stopped||!identity(object,value,hold))return nullptr;
            for(auto& slot:slots){
                auto epoch=slot.state.load(std::memory_order_acquire);
                if(epoch<2||slot.device.load(std::memory_order_relaxed)!=value.device||slot.allocation.load(std::memory_order_relaxed)!=value.allocation)continue;
                value.epoch=epoch;value.key=slot.key.load(std::memory_order_relaxed);
                if(!slot.state.compare_exchange_strong(epoch,1,std::memory_order_acq_rel))continue;
                value.retained=slot.held.load(std::memory_order_relaxed);
                if(events.releasing)events.releasing(value,key,events.opaque);
                return &slot;
            }
            return nullptr;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE acquire(IDXGIKeyedMutex* object,UINT64 key,DWORD timeout){
            auto& s=*instance.load(std::memory_order_acquire);NativeCall call;
            const auto result=s.tables[Index].acquire(object,key,timeout);const DWORD error=GetLastError();
            if(call.outer){s.acquireCalls.fetch_add(1,std::memory_order_relaxed);if(result==S_OK)s.acquireOkay.fetch_add(1,std::memory_order_relaxed);}
            // WAIT_TIMEOUT/WAIT_ABANDONED are positive HRESULT values. Neither
            // grants a lease; SUCCEEDED(result) is deliberately insufficient.
            if(call.outer&&result==S_OK&&owned(s.tables[Index]))s.acquired(object,key);
            SetLastError(error);return result;
        }
        template<unsigned Index>static HRESULT STDMETHODCALLTYPE release(IDXGIKeyedMutex* object,UINT64 key){
            auto& s=*instance.load(std::memory_order_acquire);MutexLease lease{};NativeCall call;
            if(call.outer)s.releaseCalls.fetch_add(1,std::memory_order_relaxed);
            const DWORD incoming=GetLastError();
            auto* slot=call.outer&&owned(s.tables[Index])?s.releasing(object,key,lease):nullptr;
            SetLastError(incoming);
            const auto result=s.tables[Index].release(object,key);const DWORD error=GetLastError();
            if(slot){
                if(s.events.released)s.events.released(lease,result,s.events.opaque);
                auto* held=slot->held.exchange(nullptr,std::memory_order_relaxed);if(held)held->Release();
                slot->state.store(0,std::memory_order_release);
            }
            SetLastError(error);return result;
        }
        // Cold only. Never warm, acquire or release an actual native texture.
        bool admit(IDXGIKeyedMutex* mutex,Table*& chosen)noexcept{
            void** address=nullptr;SIZE_T read=0;std::array<void*,10> entries{};
            if(!ReadProcessMemory(GetCurrentProcess(),mutex,&address,sizeof address,&read)||read!=sizeof address||!address||
                !ReadProcessMemory(GetCurrentProcess(),address,entries.data(),sizeof entries,&read)||read!=sizeof entries||!entries[8]||!entries[9])return false;
            unsigned index=0;for(;index<tables.size();++index)if(tables[index].address==address||!tables[index].address)break;
            if(index==tables.size())return false;
            auto& table=tables[index];
            if(table.address){if(!owned(table))return false;}
            else{
                DWORD previous=0;if(!writable(address,previous))return false;
                table.address=address;table.acquire=bits<Acquire>(entries[8]);table.release=bits<Release>(entries[9]);
                const std::array<void*,4> acquires{{bits<void*>(&acquire<0>),bits<void*>(&acquire<1>),bits<void*>(&acquire<2>),bits<void*>(&acquire<3>)}};
                const std::array<void*,4> releases{{bits<void*>(&release<0>),bits<void*>(&release<1>),bits<void*>(&release<2>),bits<void*>(&release<3>)}};
                table.wrappedAcquire=acquires[index];table.wrappedRelease=releases[index];
                const bool first=InterlockedCompareExchangePointer(address+8,table.wrappedAcquire,entries[8])==entries[8];
                if(first)published=true;
                const bool second=first&&InterlockedCompareExchangePointer(address+9,table.wrappedRelease,entries[9])==entries[9];
                DWORD ignored=0;const bool restored=VirtualProtect(address+8,2*sizeof(void*),previous,&ignored)!=FALSE;
                if(!second||!restored){
                    DWORD undo=0;if(writable(address,undo)){
                        InterlockedCompareExchangePointer(address+8,entries[8],table.wrappedAcquire);
                        InterlockedCompareExchangePointer(address+9,entries[9],table.wrappedRelease);
                        VirtualProtect(address+8,2*sizeof(void*),previous,&ignored);
                    }
                    return false;
                }
                tableCount.fetch_add(1,std::memory_order_relaxed);
            }
            chosen=&table;return true;
        }
        bool observeMutex(ID3D11Device* device,IDXGIKeyedMutex* mutex)noexcept{
            if(!device||!mutex||stopped||!tracked(device))return false;
            Reference<ID3D11Device> actual;
            if(FAILED(mutex->GetDevice(__uuidof(ID3D11Device),reinterpret_cast<void**>(actual.out())))||actual.value!=device)return false;
            Table* chosen=nullptr;return admit(mutex,chosen);
        }
        void observeTexture(ID3D11Device* device,ID3D11Texture2D* texture)noexcept{
            if(!device||!texture||stopped||!tracked(device))return;
            textures.fetch_add(1,std::memory_order_relaxed);
            Reference<ID3D11Device> actual;texture->GetDevice(actual.out());if(actual.value!=device)return;
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
            if(!(desc.MiscFlags&(D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX|D3D11_RESOURCE_MISC_SHARED_NTHANDLE)))return;
            sharedTextures.fetch_add(1,std::memory_order_relaxed);Reference<IDXGIKeyedMutex> mutex;
            if(FAILED(texture->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(mutex.out())))){
                nonKeyedTextures.fetch_add(1,std::memory_order_relaxed);return;
            }
            keyedTextures.fetch_add(1,std::memory_order_relaxed);
            if(!observeMutex(device,mutex.value))admissionRefused.fetch_add(1,std::memory_order_relaxed);
        }
        bool prepare(ID3D11Device* device)noexcept{
            if(!device||stopped)return false;
            Core* expected=nullptr;if(!instance.compare_exchange_strong(expected,this)&&expected!=this)return false;
            if(tracked(device))return true;
            unsigned deviceSlot=0;while(deviceSlot<devices.size()&&devices[deviceSlot].load())++deviceSlot;
            if(deviceSlot==devices.size())return false;
            D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
            Reference<ID3D11Texture2D> probe;Reference<IDXGIKeyedMutex> mutex;Reference<IDXGIResource> shared;
            if(FAILED(device->CreateTexture2D(&desc,nullptr,probe.out()))||
                FAILED(probe->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(mutex.out())))||
                FAILED(mutex->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out()))))return false;
            HANDLE handle=nullptr;if(FAILED(shared->GetSharedHandle(&handle))||!handle)return false;
            {struct Probe {Probe(){++probeDepth;}~Probe(){--probeDepth;}} probeCall;
                if(mutex->AcquireSync(0,0)!=S_OK||mutex->ReleaseSync(0)!=S_OK)return false;}
            Table* chosen=nullptr;if(!admit(mutex.value,chosen))return false;
            // Retain observed device identities for process-lifetime cached
            // callbacks. Stop may run cold; it never Releases a SINGLETHREADED
            // native device from another thread.
            device->AddRef();deviceTables[deviceSlot].store(chosen,std::memory_order_release);
            devices[deviceSlot].store(device,std::memory_order_release);return true;
        }
        void stop()noexcept{
            stopped=true;
            for(auto& table:tables)if(owned(table)){
                DWORD previous=0;if(!writable(table.address,previous))continue;
                InterlockedCompareExchangePointer(table.address+8,bits<void*>(table.acquire),table.wrappedAcquire);
                InterlockedCompareExchangePointer(table.address+9,bits<void*>(table.release),table.wrappedRelease);
                DWORD ignored=0;VirtualProtect(table.address+8,2*sizeof(void*),previous,&ignored);
            }
        }
    };
    Core* core_=new(std::nothrow) Core;
public:
    explicit MutexObserver(Events events)noexcept{if(core_)core_->events=events;}
    MutexObserver(const MutexObserver&)=delete;MutexObserver& operator=(const MutexObserver&)=delete;
    ~MutexObserver(){if(core_){core_->stop();if(!core_->published){Core* expected=core_;Core::instance.compare_exchange_strong(expected,nullptr);delete core_;}}}
    bool prepare(ID3D11Device* device)noexcept{return core_&&core_->prepare(device);}
    MutexLease current(ID3D11Device* device,std::uint64_t allocation)const noexcept{return core_?core_->current(device,allocation):MutexLease{};}
    unsigned capture(ID3D11Device* device,std::array<MutexLease,8>& output)const noexcept{return core_?core_->capture(device,output):0;}
    void observeTexture(ID3D11Device* device,ID3D11Texture2D* texture)noexcept{if(core_)core_->observeTexture(device,texture);}
    bool observeMutex(ID3D11Device* device,IDXGIKeyedMutex* mutex)noexcept{return core_&&core_->observeMutex(device,mutex);}
    Diagnostics diagnostics()const noexcept{
        if(!core_)return {};
        const auto& c=*core_;Diagnostics out{c.textures.load(),c.sharedTextures.load(),c.keyedTextures.load(),c.nonKeyedTextures.load(),c.tableCount.load(),c.admissionRefused.load(),
            c.acquireCalls.load(),c.acquireOkay.load(),c.releaseCalls.load(),c.identityRefused.load(),c.refused.load(),0,c.identityReason.load()};
        for(const auto& slot:c.slots)if(slot.state.load(std::memory_order_acquire)>=2)++out.active;
        return out;
    }
    std::uint64_t refusals()const noexcept{return core_?core_->refused.load(std::memory_order_relaxed):0;}
    void stop()noexcept{if(core_)core_->stop();}
};
} // namespace stream_bridge::sender::projected_alpha
