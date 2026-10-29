#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct ID3D11Device;
struct ID3D11Texture2D;
struct _NV_ENCODE_API_FUNCTION_LIST;

namespace stream_bridge::sender {
// This is an association key, not source permission. The caller must retain
// the existing native source/image/eye/selected-frame proofs independently.
struct MaskHevcKey {
    std::uint64_t sourceContract=0,encoderGeneration=0,submission=0;
    std::uint64_t resourceGeneration=0,mapGeneration=0,inputTimestamp=0;
    std::uint32_t processId=0,width=0,height=0;
};
bool operator==(const MaskHevcKey&,const MaskHevcKey&)noexcept;
inline bool operator!=(const MaskHevcKey& a,const MaskHevcKey& b)noexcept{return !(a==b);}

struct MaskHevcConfig {
    std::uint32_t width=0,height=0,frameRateNum=0,frameRateDen=0;
    // User ceiling, not a guarantee of AU/wire size. Reconfigure only cold.
    std::uint32_t bitRate=0,maxAccessUnitBytes=2u*1024u*1024u;
    std::uint32_t streamBitRate=0; // Original native target, before metadata deduction;0 unknown.
};
enum class MaskHevcTake { Unavailable,Busy,Missing,Pending,Ready,Refused };
enum class MaskHevcFailure : unsigned {
    None,Setup,MapInput,MapOutput,EncodePicture,LockBitstream,OutputValidation,
    UnlockBitstream,UnmapInput,PollEvent,Flush,FlushWait,Reconfigure
};
inline const char* maskHevcFailureName(MaskHevcFailure stage)noexcept{
    switch(stage){
    case MaskHevcFailure::None:return "none";case MaskHevcFailure::Setup:return "setup";
    case MaskHevcFailure::MapInput:return "map-input";case MaskHevcFailure::MapOutput:return "map-output";
    case MaskHevcFailure::EncodePicture:return "encode-picture";case MaskHevcFailure::LockBitstream:return "lock-bitstream";
    case MaskHevcFailure::OutputValidation:return "output-validation";case MaskHevcFailure::UnlockBitstream:return "unlock-bitstream";
    case MaskHevcFailure::UnmapInput:return "unmap-input";case MaskHevcFailure::PollEvent:return "poll-event";
    case MaskHevcFailure::Flush:return "flush";case MaskHevcFailure::FlushWait:return "flush-wait";
    case MaskHevcFailure::Reconfigure:return "reconfigure";
    }return "unknown";
}
// Fixed diagnostic storage only. These approximate snapshots never authorize
// a frame. Rejection bits: timestamp1,type2,structure4,hardware8,pointer16,size32,copy64.
struct MaskHevcDiagnostics {
    MaskHevcFailure stage=MaskHevcFailure::None;
    int nativeStatus=0;
    std::uint32_t rejectedFields=0,pictureType=0,pictureStruct=0,hardwareStatus=0,bytes=0;
    std::uint64_t expectedTimestamp=0,returnedTimestamp=0,mapped=0,submitted=0,locked=0,ready=0;
    std::uint32_t rateTarget=0,rateBudget=0,streamTarget=0,frameRateNum=0,frameRateDen=0,wireMean=0;
    std::uint64_t rateChanges=0;
};

// Separate Windows/D3D11 HEVC Main8 session. Each output is an independent
// IDR with repeated VPS/SPS/PPS. Y is supplied full range; UV must already be
// neutral128. No readback, upload, NVENC wrapper recursion or CPU codec path.
class MaskHevcEncoder {
public:
    static constexpr unsigned StageCount=4;
    MaskHevcEncoder();
    ~MaskHevcEncoder(); // Cold only: flushes GPU work before releasing resources.
    MaskHevcEncoder(const MaskHevcEncoder&)=delete;
    MaskHevcEncoder& operator=(const MaskHevcEncoder&)=delete;
    // Cold and quiesced. Textures must be four distinct fixed DEFAULT NV12
    // resources on this exact D3D11 device, one mip/layer, no CPU access/MSAA.
    // Original table is an owned COPY with no NvencBoundary wrappers.
    bool configure(ID3D11Device*,const MaskHevcConfig&,
                   const std::array<ID3D11Texture2D*,StageCount>&,
                   const _NV_ENCODE_API_FUNCTION_LIST& original)noexcept;
    // Before writing this texture on GPU, the caller must own its normal
    // CoverageStages FREE/RETIRED reuse claim AND obtain true here. Calling
    // this abandons any prior output for the texture. A pending encode
    // still owns its texture until completion+unmap. This is not a reservation
    // against another caller bypassing that external per-texture claim.
    bool canStage(ID3D11Texture2D*)noexcept;
    // Bind after the caller queued the exact mask conversion, before native
    // RGB Encode. submit issues the independent encode; neither method waits.
    bool beginStage(const MaskHevcKey&,ID3D11Texture2D*)noexcept;
    bool submit(const MaskHevcKey&)noexcept;
    // Polls one event with timeout0, locks with doNotWait1, and returns only
    // this exact key's IDR. Always clears out on non-Ready. Reserve out's
    // capacity cold when allocation-free output copying is required.
    MaskHevcTake tryTake(const MaskHevcKey&,std::vector<std::uint8_t>& out,std::uint64_t* rateEpoch=nullptr)noexcept;
    // Cold, opportunistic and only between private submissions. No resource
    // recreation, flush, source-generation change or invalidation on API failure.
    bool updateRate(std::uint32_t num,std::uint32_t den,std::uint32_t streamBitRate,std::uint32_t userLimit)noexcept;
    // Hot, bounded try-lock. Exact added wire bytes from this rate epoch only.
    void observeAddedBytes(std::uint64_t rateEpoch,std::size_t bytes)noexcept;
    void cancel(const MaskHevcKey&)noexcept;
    // Cold/quiesced; waits at most2s for the NVENC flush event. On false,
    // deactivates but retains the bounded GPU-live session; retry cold later.
    bool reset()noexcept;
    bool configured()const noexcept;
    int lastNvencStatus()const noexcept;
    MaskHevcDiagnostics diagnostics()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
