#pragma once
#include "stream_bridge_mask_hevc.h"

namespace stream_bridge_mask_hevc {
// Authenticated cold-worker inputs only. Temporal groups stay on one worker;
// independent groups overlap across the same four threads and eight slots. The fixed pool owns each encoded copy;
// completion order never supplies image/source/eye authority. The caller must
// rejoin the exact still-current certificate and native image lease. Lifecycle
// prepare/stop has one quiescent cold owner. Submit/take/cancel are synchronized;
// none of these methods belongs on a rendering callback.
// New groups prefer unused contexts over idle owners of an incomplete group;
// a successful final IPPP8 picture or decoder failure retires that owner hint.
class Pool {
public:
    static constexpr unsigned Workers=4,Capacity=8;
    static constexpr std::size_t MaximumPixels=8u*1024u*1024u;
    struct Result {Input identity{};Status status=Status::Unavailable;DecodedMask decoded;};
    using Wake=void(*)(void*) noexcept;
    struct PipelineStatistics:Statistics {
        std::uint64_t started=0,collected=0,queueMicros=0,maxQueueMicros=0,collectMicros=0,maxCollectMicros=0;
    };
    Pool();~Pool();
    Pool(const Pool&)=delete;Pool& operator=(const Pool&)=delete;
    bool prepareCold(const Libraries&,Wake=nullptr,void* context=nullptr)noexcept;
    bool prepareInstalledCold(Wake=nullptr,void* context=nullptr)noexcept;
    bool ready()const noexcept;
    // False preserves the caller's authenticated input for a later cold cycle.
    bool submit(const Input&)noexcept;
    bool take(Result&)noexcept;
    void cancel()noexcept; // Revoke queued/completed work; running results retire.
    void stopCold()noexcept; // Cold quiescent owner only; joins bounded workers.
    PipelineStatistics statistics()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge_mask_hevc
