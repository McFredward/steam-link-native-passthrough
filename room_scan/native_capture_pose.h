// Query the public pose history at an observed RAW clock time. No fixed clock
// offset, compositor submission counter, or current pose stands in for capture.
#pragma once
#include "native_camera.h"
#include <time.h>
namespace room_native {
inline std::uint64_t rawClockNs(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))throw std::runtime_error("Capture clock unavailable");
    return std::uint64_t(t.tv_sec)*1000000000u+std::uint64_t(t.tv_nsec);}
struct CapturePose {Mat head;V3 velocity,angularVelocity;std::uint64_t targetRaw=0,queryBeforeRaw=0,queryAfterRaw=0;};
inline CapturePose capturePose(VR_IVRSystem_FnTable* system,std::uint64_t target){
    if(!system||!system->GetDeviceToAbsoluteTrackingPose)throw std::runtime_error("Capture pose interface unavailable");
    const auto before=rawClockNs();
    // Keep the query inside the documented 100 ms sample history. Older
    // captures are unavailable, rather than clamped/backward extrapolated.
    if(target>before||before-target>80000000u)throw std::runtime_error("Capture is outside pose history");
    TrackedDevicePose_t value{};const float delta=-float(double(before-target)*1e-9);
    system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,delta,&value,1);
    const auto after=rawClockNs();
    if(after<before||after-before>3000000u||!tracked(value))throw std::runtime_error("Capture pose timing or tracking unavailable");
    return {fromNative(value.mDeviceToAbsoluteTracking),{value.vVelocity.v[0],value.vVelocity.v[1],value.vVelocity.v[2]},
        {value.vAngularVelocity.v[0],value.vAngularVelocity.v[1],value.vAngularVelocity.v[2]},target,before,after};
}
class CapturePoseSpan {
    std::array<CapturePose,9> samples_{};
public:
    Json exportSamples()const{
        Json out=Json::array();for(const auto& sample:samples_){Json matrix=Json::array();
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)matrix.push_back(sample.head.m[r][c]);
            out.push_back({{"raw_ns",sample.targetRaw},{"head",matrix},{"query_ns",sample.queryAfterRaw-sample.queryBeforeRaw}});}
        return out;
    }
    float translationSpan=0,rotationSpan=0;std::uint64_t queryBudget=0;
    void read(VR_IVRSystem_FnTable* system,std::uint64_t first,std::uint64_t last){
        if(last<first||last-first>20000000u)throw std::runtime_error("Capture pose span exceeds budget");
        translationSpan=rotationSpan=0;queryBudget=0;
        for(unsigned i=0;i<samples_.size();++i){
            samples_[i]=capturePose(system,first+(last-first)*i/(samples_.size()-1));
            queryBudget=std::max(queryBudget,samples_[i].queryAfterRaw-samples_[i].queryBeforeRaw);
            for(unsigned j=0;j<i;++j){translationSpan=std::max(translationSpan,length(position(samples_[i].head)-position(samples_[j].head)));
                float trace=0;for(unsigned c=0;c<3;++c)trace+=dot(column(samples_[i].head,c),column(samples_[j].head,c));
                rotationSpan=std::max(rotationSpan,std::acos(std::clamp((trace-1)/2,-1.f,1.f)));}
        }
    }
    Mat at(std::uint64_t raw)const{
        if(raw<samples_.front().targetRaw||raw>samples_.back().targetRaw)throw std::runtime_error("Exposure row is outside pose span");
        if(raw==samples_.front().targetRaw)return samples_.front().head;
        for(unsigned i=1;i<samples_.size();++i)if(raw<=samples_[i].targetRaw){
            const auto& a=samples_[i-1];const auto& b=samples_[i];
            const float t=float(double(raw-a.targetRaw)/double(b.targetRaw-a.targetRaw));
            auto q=quaternion(a.head),r=quaternion(b.head);float sign=0;for(unsigned j=0;j<4;++j)sign+=q[j]*r[j];
            float norm=0;for(unsigned j=0;j<4;++j){q[j]=q[j]*(1-t)+r[j]*(sign<0?-t:t);norm+=q[j]*q[j];}
            norm=std::sqrt(norm);for(auto& v:q)v/=norm;
            return pose(position(a.head)*(1-t)+position(b.head)*t,q);
        }
        throw std::runtime_error("Capture pose interpolation unavailable");
    }
};
} // namespace room_native
