#pragma once
#include <openvr_driver.h>
#include "stream-bridge-sender-provider-swap-shape.h"
#include <array>
#include <atomic>
#include <mutex>
namespace stream_bridge::sender {
using DirectMode=vr::IVRDriverDirectModeComponent;
inline bool decodeSwapTextureSetDesc(const DirectMode::SwapTextureSetDesc_t& d,
    SharedTextureShape& shape)noexcept{
    return decodePinnedSwapShape(d.nWidth,d.nHeight,d.nFormat,d.nSampleCount,shape);
}
struct DirectEye {
    DirectMode::SubmitLayerPerEye_t submitted{};
    DirectMode::SwapTextureSetDesc_t description{};
    std::uint64_t swapGeneration=0;std::uint32_t processId=0;
};
struct DirectFrame {
    void* component=nullptr;std::uint64_t presentSerial=0,registryEpoch=0;
    std::array<std::array<DirectEye,2>,4> layers{};std::uint32_t layerCount=0;
    bool sourceKnown=false;
};
struct DirectSwap {void* component=nullptr;std::uint32_t processId=0;std::uint64_t generation=0;DirectMode::SwapTextureSetDesc_t description{};std::array<vr::SharedTextureHandle_t,3> handles{};};
struct DirectInventory {std::array<DirectSwap,64> swaps{};std::uint64_t epoch=0;bool valid=false;};
// Cold diagnostics only; counts requests rather than frames. No source proof.
struct DirectCleanupStats{std::uint64_t requests=0,empty=0,changed=0,lockMisses=0;};
// Approximate cold diagnostics, never source/frame authority. Callbacks only
// increment fixed atomics and try once to retain a rejected descriptor/bounds.
enum class DirectDiagnostic : unsigned {None,CreateShape,CreateLock,CreateHandles,CreateDuplicate,CreateCapacity,
    SubmitLock,SubmitOverflow,SubmitUnknown,SubmitGeometry,SubmitPid,ComponentCapacity,PresentLock,PresentEmpty,PresentTainted,
    NoScope,UnknownSource,StaleEpoch,Disabled,Count};
inline const char* directDiagnosticName(DirectDiagnostic reason)noexcept{
    switch(reason){
    case DirectDiagnostic::CreateShape:return "create-shape";
    case DirectDiagnostic::CreateLock:return "create-lock";
    case DirectDiagnostic::CreateHandles:return "create-handles";
    case DirectDiagnostic::CreateDuplicate:return "create-duplicate";
    case DirectDiagnostic::CreateCapacity:return "create-capacity";
    case DirectDiagnostic::SubmitUnknown:return "submit-unknown";
    case DirectDiagnostic::SubmitGeometry:return "submit-geometry";
    case DirectDiagnostic::SubmitPid:return "submit-pid";
    case DirectDiagnostic::SubmitOverflow:return "submit-overflow";
    default:return "other";
    }
}
struct DirectRejected {
    DirectDiagnostic reason=DirectDiagnostic::None;
    std::uint32_t pid=0,width=0,height=0,format=0,packedSamples=0,layers=0;
    std::array<float,4> bounds{};
};
struct DirectDiagnosticStats {
    std::array<std::uint64_t,static_cast<unsigned>(DirectDiagnostic::Count)> counts{};
    DirectRejected create{},submit{};bool contextAvailable=false,enabled=false;
    int processSwaps=-1; // -1: registry busy, not proof of missing ownership.
    std::uint64_t count(DirectDiagnostic value)const noexcept{return counts[static_cast<unsigned>(value)];}
};
struct DirectModeFunctions {
    void (*create)(void*,std::uint32_t,const DirectMode::SwapTextureSetDesc_t*,DirectMode::SwapTextureSet_t*)=nullptr;
    void (*destroy)(void*,vr::SharedTextureHandle_t)=nullptr;
    void (*destroyAll)(void*,std::uint32_t)=nullptr;
    void (*next)(void*,vr::SharedTextureHandle_t[2],std::uint32_t(*)[2])=nullptr;
    void (*submit)(void*,const DirectMode::SubmitLayerPerEye_t(&)[2])=nullptr;
    void (*present)(void*,vr::SharedTextureHandle_t)=nullptr;
    void (*post)(void*,const DirectMode::Throttling_t*)=nullptr;
    void (*timing)(void*,vr::DriverDirectMode_FrameTiming*)=nullptr;
};
// Public DirectMode009 calls only. Native installer verifies all eight concrete
// original pointers before publication. Metadata never acquires GPU resources or
// proves alpha/mapping; the actual native composition must supply those facts.
class DirectModeBoundary {
public:
    bool prepare(const DirectModeFunctions&) noexcept;
    DirectModeFunctions entryPoints()const noexcept;
    void stop()noexcept;
    bool ownsProcess(std::uint32_t)const noexcept;
    // Cold change detection only; never certifies a texture/frame. An unchanged
    // registry needs no repeated lock/copy that can contend with native Submit.
    std::uint64_t inventoryRevision()const noexcept;
    DirectInventory inventory()const noexcept;
    DirectCleanupStats cleanupStats()const noexcept;
    DirectDiagnosticStats diagnostics(std::uint32_t processId)const noexcept;
    static const DirectFrame* currentFrame()noexcept;
private:
    struct Swap{void* component=nullptr;std::uint32_t pid=0;std::uint64_t generation=0;DirectMode::SwapTextureSetDesc_t description{};std::array<vr::SharedTextureHandle_t,3> handles{};};
    struct Component{void* value=nullptr;DirectFrame queued{},presented{};};
    static void create(void*,std::uint32_t,const DirectMode::SwapTextureSetDesc_t*,DirectMode::SwapTextureSet_t*);
    static void destroy(void*,vr::SharedTextureHandle_t);
    static void destroyAll(void*,std::uint32_t);
    static void next(void*,vr::SharedTextureHandle_t[2],std::uint32_t(*)[2]);
    static void submit(void*,const DirectMode::SubmitLayerPerEye_t(&)[2]);
    static void present(void*,vr::SharedTextureHandle_t);
    static void post(void*,const DirectMode::Throttling_t*);
    static void timing(void*,vr::DriverDirectMode_FrameTiming*);
    Component* component(void*);
    void diagnose(DirectDiagnostic,std::uint32_t pid=0,const DirectMode::SwapTextureSetDesc_t* description=nullptr,
                  const DirectMode::SubmitLayerPerEye_t* eye=nullptr,std::uint32_t layers=0)noexcept;
    DirectModeFunctions original_{};mutable std::mutex mutex_;
    std::array<Swap,64> swaps_{};std::array<Component,4> components_{};
    std::atomic<std::uint64_t> destroyRequests_{0},emptyDestroys_{0},destroyChanges_{0},destroyLockMisses_{0};
    std::array<std::atomic<std::uint64_t>,static_cast<unsigned>(DirectDiagnostic::Count)> diagnosticCounts_{};
    mutable std::atomic_flag diagnosticContext_=ATOMIC_FLAG_INIT;
    DirectRejected rejectedCreate_{},rejectedSubmit_{};
    std::uint64_t nextSwap_=0,nextPresent_=0;bool prepared_=false;
    std::atomic<std::uint64_t> registryEpoch_{1};std::atomic<bool> enabled_{false},tainted_{false};
    static std::atomic<DirectModeBoundary*> instance_;
    static thread_local const DirectFrame* current_;
};
} // namespace stream_bridge::sender
