#pragma once
#include "stream-bridge-projected-alpha-sync.h"
#include <bcrypt.h>
#include <type_traits>

namespace stream_bridge::sender::projected_alpha {
// PRIVATE production transport, deliberately separate from SourceAudit.V1.
// A verified loader duplicates an UNNAMED section handle into its exact peer.
// No public mapping name, diagnostic record, PID guess or latest timestamp can
// supply this channel. Possessing the channel is still not image authority:
// native resource ancestry and actual image/lock proofs remain mandatory.
constexpr std::uint64_t ChannelMagic=0x324c414e48435053ull;
constexpr unsigned ChannelAllocations=192,ChannelImages=32;
struct Allocation {
    std::uint64_t handle=0,generation=0;
    std::uint32_t pid=0,width=0,height=0,format=0,samples=0,arraySize=0;
};
struct Registry {
    // Published ONLY from current, retained native opens plus the live
    // kernel-validated application lease. Never copy a diagnostic inventory.
    std::uint64_t sourceLease=0,revision=0;
    std::uint64_t adapterLuid=0;
    std::uint32_t adapterKnown=0;
    std::array<Allocation,ChannelAllocations> retained{};
};
struct MaskRegistration {
    Allocation native{};
    std::uint64_t maskHandle=0,maskGeneration=0;
};
struct ImageWitness {
    struct Release {std::uint64_t allocation=0,scope=0,key=0;};
    Allocation native{},scene{};
    std::uint64_t sourceLease=0,maskHandle=0,maskGeneration=0;
    std::uint64_t syncAllocation=0,producerScope=0,releaseKey=0,imageVersion=0;
    // A frame can hold both an app lock and the stream lock. Seal each observed
    // matching release; the consumer selects its actual Present sync texture.
    std::array<Release,8> releases{};
    // Not a frame counter. The producer invalidates BEFORE ANY native output
    // write. A witness is published only for that exact RGB/coverage version
    // under observed native ownership; it holds its sync allocation alive.
    std::uint32_t complete=0,route=0;
    // Native reprojection can combine several retained colour allocations
    // from the same live game. Preserve every actual contributor, including
    // those retained unchanged through a partial native image update.
    std::array<Allocation,8> contributors{};
    std::uint32_t contributorCount=0;
    std::uint64_t nativeMutation=0; // Invalidates writes; never substitutes for native Present ownership.
};
template<class T>struct Row {
    alignas(8) volatile LONG64 sequence=0;T value{};
    bool publish(const T& next)noexcept{
        const auto before=InterlockedCompareExchange64(&sequence,0,0);
        if(before<0||(before&1)||before>std::numeric_limits<LONG64>::max()-2)return false;
        InterlockedIncrement64(&sequence);MemoryBarrier();
        std::memcpy(&value,&next,sizeof next);MemoryBarrier();InterlockedIncrement64(&sequence);return true;
    }
    bool read(T& output)const noexcept{
        auto* seq=const_cast<volatile LONG64*>(&sequence);
        const auto before=InterlockedCompareExchange64(seq,0,0);if(before<=0||(before&1))return false;
        std::memcpy(&output,&value,sizeof output);MemoryBarrier();
        return InterlockedCompareExchange64(seq,0,0)==before;
    }
};
struct ChannelPage {
    std::uint64_t magic=ChannelMagic,parentBirth=0,peerBirth=0;
    std::array<std::uint8_t,16> cookie{};
    std::uint32_t bytes=sizeof(ChannelPage),parentPid=0,peerPid=0,reserved=0;
    volatile LONG bound=0,stopped=0,peerReady=0;
    volatile LONG nextImageSlot=0;
    // Private process-pair admission: count, dirty bit and monotonic overlap epoch.
    alignas(8) volatile LONG64 captureAdmission=0;
    // Separate bounded pools: ordinary RGBA/MSAA targets cannot consume the
    // storage needed by the native FP16 X/Y path. Each remains <=256 MiB;
    // the total admitted companion storage is therefore <=512 MiB.
    alignas(8) volatile LONG64 companionBytes=0,fp16CompanionBytes=0;
    // Nonblocking publication gate. A reader keeps published allocations pinned
    // until it has retained the actual native source binding. No stale numeric
    // KMT value can be reclassified after its previous allocation is destroyed.
    volatile LONG registryGate=0;
    alignas(8) volatile LONG64 sourceLease=0;
    Row<Registry> registry{};
    std::array<Row<MaskRegistration>,ChannelImages> registrations{};
    std::array<Row<ImageWitness>,ChannelImages> witnesses{};
    alignas(8) volatile LONG64 imageMutations[ChannelImages]{};
};
static_assert(std::is_trivially_copyable<Registry>::value&&std::is_trivially_copyable<ImageWitness>::value,
    "fixed private production payloads");
static_assert(sizeof(ChannelPage)<=32768,"bounded private section");
// One producer owner at a time; rejected callbacks retain a counted guard until
// their ORIGINAL native operation returns. No waiting or stale witness recovery.
class CaptureAdmission {
    ChannelPage* page_=nullptr;std::uint64_t token_=0;bool exclusive_=false;
public:
    static constexpr std::uint64_t Count=127,GeometryUnknown=128,Dirty=256,Step=512;
    static std::uint64_t state(ChannelPage* page)noexcept{
        return page?std::uint64_t(InterlockedCompareExchange64(&page->captureAdmission,0,0)):Dirty;
    }
    static std::uint64_t epoch(ChannelPage* page)noexcept{return state(page)&~Count;}
    static bool clean(ChannelPage* page)noexcept{return page&&!(state(page)&Dirty)&&!InterlockedCompareExchange(&page->stopped,0,0);}
    CaptureAdmission()noexcept=default;
    explicit CaptureAdmission(ChannelPage* page,bool geometryWrite=false)noexcept{
        if(!page||InterlockedCompareExchange(&page->stopped,0,0))return;
        auto before=state(page);
        for(unsigned attempt=0;attempt<16;++attempt){
            const auto count=before&Count;
            if(count==Count||before>std::uint64_t(INT64_MAX)-Step-1)break;
            auto after=before+1;
            if(count)after=(after|Dirty|(geometryWrite?GeometryUnknown:0))+Step;
            const auto observed=std::uint64_t(InterlockedCompareExchange64(&page->captureAdmission,LONG64(after),LONG64(before)));
            if(observed==before){page_=page;token_=after;exclusive_=count==0;return;}
            before=observed;
        }
        InterlockedExchange(&page->stopped,1); // Unrepresentable/contended admission cannot grant pixels.
    }
    CaptureAdmission(const CaptureAdmission&)=delete;CaptureAdmission& operator=(const CaptureAdmission&)=delete;
    CaptureAdmission(CaptureAdmission&& other)noexcept:page_(other.page_),token_(other.token_),exclusive_(other.exclusive_){other.page_=nullptr;}
    ~CaptureAdmission(){if(page_)InterlockedDecrement64(&page_->captureAdmission);}
    bool tracked()const noexcept{return page_!=nullptr;}
    bool exclusive()const noexcept{return exclusive_;}
    bool dirty()const noexcept{return (token_&Dirty)!=0;}
    bool geometryUnknown()const noexcept{return (token_&GeometryUnknown)!=0;}
    bool recover()noexcept{
        // The caller first invalidates ALL producer ancestry and shared versions.
        // A new overlap during invalidation changes the epoch, so cannot be cleared.
        return page_&&exclusive_&&InterlockedCompareExchange64(&page_->captureAdmission,
            LONG64(token_&~(Dirty|GeometryUnknown)),LONG64(token_))==LONG64(token_);
    }
};
// Every observed write invalidates all registered aliases of that actual
// shared kernel allocation, including writers on another native device. This
// counter grants nothing: selection still requires the exact native release,
// current retained generations, source lease and original frame/eye proof.
inline void invalidateSharedImages(ChannelPage* page,const Allocation& actual)noexcept{
    if(!page||!actual.handle)return;
    for(unsigned i=0;i<ChannelImages;++i){MaskRegistration registration{};
        if(!page->registrations[i].read(registration)||registration.native.handle!=actual.handle||
            registration.native.width!=actual.width||registration.native.height!=actual.height||registration.native.format!=actual.format||
            registration.native.samples!=actual.samples||registration.native.arraySize!=actual.arraySize)continue;
        if(InterlockedIncrement64(&page->imageMutations[i])<=0)InterlockedExchange(&page->stopped,1);
    }
}
inline void invalidateSharedImages(ChannelPage* page,ID3D11Resource* resource)noexcept{
    if(!page)return;
    if(!resource){for(auto& value:page->imageMutations)if(InterlockedIncrement64(&value)<=0)InterlockedExchange(&page->stopped,1);return;}
    Reference<ID3D11Texture2D> texture;Reference<IDXGIResource> shared;HANDLE handle=nullptr;
    if(FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||
        FAILED(resource->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))||
        FAILED(shared->GetSharedHandle(&handle))||!handle)return;
    D3D11_TEXTURE2D_DESC shape{};texture->GetDesc(&shape);
    invalidateSharedImages(page,Allocation{std::uint64_t(reinterpret_cast<std::uintptr_t>(handle)),0,0,
        shape.Width,shape.Height,unsigned(shape.Format),shape.SampleDesc.Count,shape.ArraySize});
}
constexpr std::uint64_t CompanionClassBudget=256ull*1024*1024;
// Cold resource creation only. Failed preparation returns its reservation,
// including early exits after the image exists but its clear failed.
class CompanionReservation {
    volatile LONG64* counter_=nullptr;LONG64 bytes_=0;
public:
    CompanionReservation()=default;
    CompanionReservation(const CompanionReservation&)=delete;
    CompanionReservation& operator=(const CompanionReservation&)=delete;
    ~CompanionReservation(){if(counter_)InterlockedExchangeAdd64(counter_,-bytes_);}
    bool claim(ChannelPage* page,bool fp16,std::uint64_t bytes)noexcept{
        if(counter_||!page||!bytes||bytes>CompanionClassBudget)return false;
        auto* counter=fp16?&page->fp16CompanionBytes:&page->companionBytes;
        for(unsigned attempt=0;attempt<8;++attempt){
            const auto before=InterlockedCompareExchange64(counter,0,0);
            if(before<0||std::uint64_t(before)>CompanionClassBudget-bytes)return false;
            if(InterlockedCompareExchange64(counter,before+LONG64(bytes),before)!=before)continue;
            counter_=counter;bytes_=LONG64(bytes);return true;
        }
        return false;
    }
    void commit()noexcept{counter_=nullptr;bytes_=0;}
};
struct ChannelLaunch {
    std::uint64_t magic=ChannelMagic,parentBirth=0,peerBirth=0,mapping=0;
    std::array<std::uint8_t,16> cookie{};
    std::uint32_t parentPid=0,peerPid=0,bytes=sizeof(ChannelLaunch),reserved=0;
};
inline std::uint64_t processBirth(HANDLE process)noexcept{
    FILETIME created{},exit{},kernel{},user{};
    return GetProcessTimes(process,&created,&exit,&kernel,&user)?
        (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime:0;
}
inline bool deviceLuid(ID3D11Device* device,std::uint64_t& value)noexcept{
    Reference<IDXGIDevice> dxgi;Reference<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC desc{};
    if(!device||FAILED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void**>(dxgi.out())))||
        FAILED(dxgi->GetAdapter(adapter.out()))||FAILED(adapter->GetDesc(&desc)))return false;
    value=(std::uint64_t(static_cast<std::uint32_t>(desc.AdapterLuid.HighPart))<<32)|desc.AdapterLuid.LowPart;return true;
}
class RegistryRead {
    ChannelPage* page_=nullptr;Registry snapshot_{};
public:
    RegistryRead()=default;RegistryRead(const RegistryRead&)=delete;RegistryRead& operator=(const RegistryRead&)=delete;
    ~RegistryRead(){release();}
    bool acquire(ChannelPage* page)noexcept{
        if(page_||!page||InterlockedCompareExchange(&page->stopped,0,0))return false;
        const auto readers=InterlockedCompareExchange(&page->registryGate,0,0);
        if(readers<0||readers>=64||InterlockedCompareExchange(&page->registryGate,readers+1,readers)!=readers)return false;
        page_=page;
        if(!page->registry.read(snapshot_)||snapshot_.sourceLease!=static_cast<std::uint64_t>(InterlockedCompareExchange64(&page->sourceLease,0,0))){release();return false;}
        return true;
    }
    const Registry& snapshot()const noexcept{return snapshot_;}
    void release()noexcept{if(page_){InterlockedDecrement(&page_->registryGate);page_=nullptr;}}
};
class ChannelParent {
    HANDLE mapping_=nullptr;ChannelPage* page_=nullptr;
    std::array<ID3D11Texture2D*,ChannelAllocations> pins_{};
public:
    ChannelParent()=default;ChannelParent(const ChannelParent&)=delete;ChannelParent& operator=(const ChannelParent&)=delete;
    ~ChannelParent(){stop();for(auto* pin:pins_)if(pin)pin->Release();if(page_)UnmapViewOfFile(page_);if(mapping_)CloseHandle(mapping_);}
    bool create()noexcept{
        if(mapping_||page_)return false;
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ChannelPage),nullptr);
        if(!mapping_)return false;
        page_=static_cast<ChannelPage*>(MapViewOfFile(mapping_,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(ChannelPage)));
        if(!page_)return false;
        new(page_) ChannelPage{};page_->parentPid=GetCurrentProcessId();page_->parentBirth=processBirth(GetCurrentProcess());
        if(!page_->parentBirth||BCryptGenRandom(nullptr,page_->cookie.data(),static_cast<ULONG>(page_->cookie.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0){stop();return false;}
        return true;
    }
    // Call only after verifying the peer's user/session, publisher, exact file
    // identity and process birth. The existing verified loader owns that check.
    // One attempt; an uncertain duplicate is never blindly replayed.
    bool duplicateToVerifiedPeer(HANDLE process,ChannelLaunch& launch)noexcept{
        launch={};if(!page_||page_->peerPid||InterlockedCompareExchange(&page_->stopped,0,0))return false;
        const auto pid=GetProcessId(process);const auto birth=processBirth(process);
        if(!pid||!birth)return false;
        page_->peerPid=pid;page_->peerBirth=birth;
        HANDLE remote=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),mapping_,process,&remote,0,FALSE,DUPLICATE_SAME_ACCESS))return false;
        launch.parentPid=page_->parentPid;launch.parentBirth=page_->parentBirth;
        launch.peerPid=pid;launch.peerBirth=birth;launch.cookie=page_->cookie;
        launch.mapping=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(remote));
        InterlockedExchange(&page_->bound,1);return true;
    }
    ChannelPage* page()const noexcept{return page_;}
    void sourceLease(std::uint64_t value)noexcept{if(page_)InterlockedExchange64(&page_->sourceLease,static_cast<LONG64>(value));}
    // Cold, single publisher. Caller obtains PID/generation only from its real
    // DirectMode registry, with live native opens. Shape/handle/LUID are checked
    // independently against those retained kernel allocations here. The reader
    // gate prevents dropping/recycling the old pins during source admission.
    bool publishRetained(const Registry& requested,const std::array<ID3D11Texture2D*,ChannelAllocations>& resources)noexcept{
        if(!page_||InterlockedCompareExchange(&page_->stopped,0,0)||
            InterlockedCompareExchange(&page_->registryGate,-1,0)!=0)return false;
        struct Unlock {ChannelPage* page;~Unlock(){InterlockedExchange(&page->registryGate,0);}} unlock{page_};
        Registry actual=requested;actual.adapterKnown=0;actual.adapterLuid=0;
        std::array<Reference<ID3D11Texture2D>,ChannelAllocations> next{};
        for(unsigned i=0;i<requested.retained.size();++i){
            const auto& item=requested.retained[i];if(!item.handle)continue;
            if(!item.generation||!item.pid||!resources[i])return false;
            for(unsigned prior=0;prior<i;++prior)if(requested.retained[prior].handle==item.handle)return false;
            D3D11_TEXTURE2D_DESC desc{};resources[i]->GetDesc(&desc);
            if(desc.Width!=item.width||desc.Height!=item.height||unsigned(desc.Format)!=item.format||desc.SampleDesc.Count!=item.samples||
                desc.ArraySize!=item.arraySize||desc.MipLevels!=1||desc.Usage!=D3D11_USAGE_DEFAULT||desc.CPUAccessFlags)return false;
            Reference<IDXGIResource> shared;HANDLE handle=nullptr;
            if(FAILED(resources[i]->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))||
                FAILED(shared->GetSharedHandle(&handle))||static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle))!=item.handle)return false;
            Reference<ID3D11Device> device;resources[i]->GetDevice(device.out());std::uint64_t luid=0;
            if(!deviceLuid(device.value,luid)||(actual.adapterKnown&&luid!=actual.adapterLuid))return false;
            actual.adapterKnown=1;actual.adapterLuid=luid;resources[i]->AddRef();next[i].value=resources[i];
        }
        if(actual.sourceLease!=static_cast<std::uint64_t>(InterlockedCompareExchange64(&page_->sourceLease,0,0))||!page_->registry.publish(actual))return false;
        for(unsigned i=0;i<pins_.size();++i){if(pins_[i])pins_[i]->Release();pins_[i]=next[i].value;next[i].value=nullptr;}
        return true;
    }
    void stop()noexcept{if(page_)InterlockedExchange(&page_->stopped,1);}
};
class ChannelPeer {
    HANDLE mapping_=nullptr,parent_=nullptr;ChannelPage* page_=nullptr;
public:
    ChannelPeer()=default;ChannelPeer(const ChannelPeer&)=delete;ChannelPeer& operator=(const ChannelPeer&)=delete;
    ~ChannelPeer(){if(page_)UnmapViewOfFile(page_);if(mapping_)CloseHandle(mapping_);if(parent_)CloseHandle(parent_);}
    bool open(const ChannelLaunch& launch)noexcept{
        if(page_||mapping_||parent_||launch.magic!=ChannelMagic||launch.bytes!=sizeof(ChannelLaunch)||launch.reserved||
            !launch.mapping||!launch.parentPid||!launch.parentBirth||launch.peerPid!=GetCurrentProcessId()||
            launch.peerBirth!=processBirth(GetCurrentProcess()))return false;
        const auto candidate=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(launch.mapping));
        auto* view=static_cast<ChannelPage*>(MapViewOfFile(candidate,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(ChannelPage)));
        if(!view)return false;
        const bool matching=view->magic==ChannelMagic&&view->bytes==sizeof(ChannelPage)&&view->cookie==launch.cookie&&
            view->parentPid==launch.parentPid&&view->parentBirth==launch.parentBirth&&
            view->peerPid==launch.peerPid&&view->peerBirth==launch.peerBirth&&
            InterlockedCompareExchange(&view->bound,0,0)==1&&!InterlockedCompareExchange(&view->stopped,0,0);
        if(!matching){UnmapViewOfFile(view);return false;} // Never close an unverified caller's handle.
        const auto parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,launch.parentPid);
        if(!parent||processBirth(parent)!=launch.parentBirth||WaitForSingleObject(parent,0)!=WAIT_TIMEOUT){
            if(parent)CloseHandle(parent);
            UnmapViewOfFile(view);return false;
        }
        page_=view;mapping_=candidate;parent_=parent;InterlockedExchange(&page_->peerReady,1);return true;
    }
    ChannelPage* page()const noexcept{return page_;}
    bool active()const noexcept{return page_&&parent_&&!InterlockedCompareExchange(&page_->stopped,0,0)&&
        WaitForSingleObject(parent_,0)==WAIT_TIMEOUT;}
};
} // namespace stream_bridge::sender::projected_alpha
