#pragma once
#include "stream-bridge-sender-provider-core.h"
#include "../bridge/stream_bridge_control.h"
#include <atomic>
#include <limits>

namespace stream_bridge::sender {
// Native dashboard intent is not an image/coverage witness. Freeze its epoch
// in an exact native Encode receipt, then require the same intent at Lock.
// Missing alpha or an unknown draw never establishes an ordinary picture.
class NativePresentation {
    std::atomic<std::uint64_t> state_{0},certified_{0},refused_{0},activated_{0},deactivated_{0};
    std::atomic<bool> stopped_{false};
public:
    static constexpr unsigned DashboardActivated=502,DashboardDeactivated=503;
    explicit NativePresentation(std::uint64_t initial=0)noexcept:state_(initial){}
    void dashboard(bool visible)noexcept {
        if(stopped_.load(std::memory_order_acquire))return;
        const auto before=state_.load(std::memory_order_relaxed);
        if(bool(before&1)==visible)return;
        if(before==std::numeric_limits<std::uint64_t>::max()){stop();return;}
        state_.store(before+1,std::memory_order_release);
        if(visible)++activated_;else ++deactivated_;
    }
    // RunFrame owner only: read its own public host without changing overlays
    // or synthesizing input. No unbounded event draining or render-thread I/O.
    template<class Poll> void poll(Poll next)noexcept {
        for(unsigned n=0;n<64;++n){unsigned event=0;if(!next(event))break;
            if(event==DashboardActivated)dashboard(true);
            else if(event==DashboardDeactivated)dashboard(false);
        }
    }
    std::uint64_t snapshot()const noexcept {
        const auto state=state_.load(std::memory_order_acquire);
        return !stopped_.load(std::memory_order_acquire)&&(state&1)?state:0;
    }
    bool current(std::uint64_t receipt)const noexcept{return receipt&&snapshot()==receipt;}
    std::uint8_t policy(std::uint64_t receipt,bool recovery,bool ordinary)const noexcept {
        return current(receipt)&&recovery?(ordinary?3:2):0;
    }
    bool certify(ControlRuntime& control,Bytes video,const NvencOutputEvidence& e,
                 std::vector<std::uint8_t>& output)noexcept {
        output.clear();
        const auto selected=policy(e.nativePresentation,e.cameraOnlyRecoveryAuthorized,e.ordinaryVideoAuthorized);
        if(!e.exactInput||!e.submission||!e.encoderGeneration||!e.coverageSessionAuthorized||!selected||
           !e.width||!e.height||e.width>8192||e.height>8192){++refused_;return false;}
        const FrameDescriptor descriptor{e.submission,e.encoderGeneration,
            static_cast<std::uint16_t>(e.width),static_cast<std::uint16_t>(e.height),selected};
        if(!control.certifyAccessUnit(video,descriptor,CoverageEncoding::None,{},output)||!current(e.nativePresentation)){
            output.clear();++refused_;return false;
        }
        ++certified_;return true;
    }
    std::array<std::uint64_t,5> status()const noexcept {
        return {snapshot(),certified_.load(std::memory_order_relaxed),refused_.load(std::memory_order_relaxed),activated_.load(std::memory_order_relaxed),deactivated_.load(std::memory_order_relaxed)};
    }
    void stop()noexcept{stopped_.store(true,std::memory_order_release);}
};
} // namespace stream_bridge::sender
