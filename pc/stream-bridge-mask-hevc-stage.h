#pragma once
#include "stream-bridge-mask-hevc.h"
namespace stream_bridge::sender {
struct MaskHevcStageState {
    bool hevcSurfaceReady=false,hevcSubmitted=false;
    MaskHevcKey hevcKey{};
};
// Caller exclusively owns every CoverageStages maintenance claim. Captured A8
// receipts may survive a cold reset, but never proof of writes to retired NV12
// surfaces. Invalidate before reset, including its failure/retention path.
template<class Pool>void invalidateMaskHevcStages(Pool& pool)noexcept{
    for(auto& slot:pool){auto& state=slot->resources;
        state.hevcSurfaceReady=false;state.hevcSubmitted=false;state.hevcKey={};
    }
}
}
