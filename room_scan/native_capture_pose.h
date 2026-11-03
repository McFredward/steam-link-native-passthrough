// Query the public pose history at an observed RAW clock time. No fixed clock
// offset, compositor submission counter, or current pose stands in for capture.
#pragma once
#include "native_camera.h"
#include <time.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
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
// Camera dequeue can lag exposure by more than the public 80 ms query window.
// Keep actual, timestamp-bracketed current-pose observations in bounded memory.
// A span contains nine real samples; never extrapolate an old image from now.
class CapturePoseHistory {
    mutable std::mutex mutex_;std::deque<CapturePose> samples_;
    std::atomic<bool> stopping_{true};std::thread worker_;
public:
    ~CapturePoseHistory(){stop();}
    void clear(){std::lock_guard<std::mutex> lock(mutex_);samples_.clear();}
    void append(const CapturePose& sample){
        std::lock_guard<std::mutex> lock(mutex_);
        if(sample.queryAfterRaw<sample.queryBeforeRaw||sample.queryAfterRaw-sample.queryBeforeRaw>750000u
            ||sample.targetRaw<sample.queryBeforeRaw||sample.targetRaw>sample.queryAfterRaw||!rigid(sample.head)){
            samples_.clear();return;}
        if(!samples_.empty()&&sample.targetRaw<=samples_.back().targetRaw)samples_.clear();
        samples_.push_back(sample);
        while(samples_.size()>1024||sample.targetRaw-samples_.front().targetRaw>600000000u)samples_.pop_front();
    }
    void start(VR_IVRSystem_FnTable* system){
        stop();if(!system||!system->GetDeviceToAbsoluteTrackingPose)throw std::runtime_error("Capture history interface unavailable");
        stopping_=false;worker_=std::thread([this,system]{while(!stopping_){
            try{
                const auto before=rawClockNs();TrackedDevicePose_t value{};
                system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&value,1);
                const auto after=rawClockNs();if(after<before||after-before>750000u||!tracked(value))clear();
                else append({fromNative(value.mDeviceToAbsoluteTracking),{value.vVelocity.v[0],value.vVelocity.v[1],value.vVelocity.v[2]},
                    {value.vAngularVelocity.v[0],value.vAngularVelocity.v[1],value.vAngularVelocity.v[2]},before+(after-before)/2,before,after});
            }catch(const std::exception&){clear();}
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }});
    }
    void stop(){stopping_=true;if(worker_.joinable())worker_.join();clear();}
    bool span(std::uint64_t first,std::uint64_t last,std::array<CapturePose,9>& out)const{
        const auto now=rawClockNs();if(last<first||first>now||now-first>500000000u)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(samples_.size()<out.size()||first<samples_.front().targetRaw||last>samples_.back().targetRaw)return false;
        std::size_t left=0,right=0;
        while(left+1<samples_.size()&&samples_[left+1].targetRaw<=first)++left;
        right=left;while(right<samples_.size()&&samples_[right].targetRaw<last)++right;
        while(right-left+1<out.size()){
            if(left&&(!((right+1)<samples_.size())||first-samples_[left].targetRaw<=samples_[right].targetRaw-last))--left;
            else if(right+1<samples_.size())++right;
            else return false;
        }
        if(samples_[right].targetRaw-samples_[left].targetRaw>20000000u)return false;
        for(unsigned i=0;i<out.size();++i){out[i]=samples_[left+(right-left)*i/(out.size()-1)];
            if(i&&(out[i].targetRaw<=out[i-1].targetRaw||out[i].targetRaw-out[i-1].targetRaw>4000000u))return false;}
        return true;
    }
};
class CapturePoseSpan {
    std::array<CapturePose,9> samples_{};
public:
    Json exportSamples()const{
        Json out=Json::array();for(const auto& sample:samples_){Json matrix=Json::array();
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)matrix.push_back(sample.head.m[r][c]);
            out.push_back({{"raw_ns",sample.targetRaw},{"head",matrix},{"query_ns",sample.queryAfterRaw-sample.queryBeforeRaw}});}
        return out;
    }
    float translationSpan=0,rotationSpan=0;std::uint64_t queryBudget=0;bool sampledHistory=false;
    void read(VR_IVRSystem_FnTable* system,std::uint64_t first,std::uint64_t last,const CapturePoseHistory* history=nullptr){
        if(last<first||last-first>20000000u)throw std::runtime_error("Capture pose span exceeds budget");
        translationSpan=rotationSpan=0;queryBudget=0;
        const bool cached=history&&history->span(first,last,samples_);
        sampledHistory=cached;
        for(unsigned i=0;i<samples_.size();++i){
            if(!cached)samples_[i]=capturePose(system,first+(last-first)*i/(samples_.size()-1));
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
    bool supportsTimeUncertainty(std::uint64_t ns)const{
        if(!ns)return true;
        const double uncertainty=double(ns)*1e-9;
        for(unsigned i=1;i<samples_.size();++i){const auto& a=samples_[i-1];const auto& b=samples_[i];
            if(b.targetRaw<=a.targetRaw)return false;
            const double dt=double(b.targetRaw-a.targetRaw)*1e-9;
            auto q=quaternion(a.head),r=quaternion(b.head);double sign=0,difference=0;
            for(unsigned j=0;j<4;++j)sign+=double(q[j])*r[j];
            for(unsigned j=0;j<4;++j){const double v=double(q[j])-(sign<0?-double(r[j]):double(r[j]));difference+=v*v;}
            const double angle=4*std::asin(std::min(1.,std::sqrt(difference)/2));
            if(double(length(position(b.head)-position(a.head)))*uncertainty/dt>.0005||angle*uncertainty/dt>.0005)return false;
        }
        return true;
    }
};
} // namespace room_native
