#include "room_scan/native_arcturus_geometry.h"
#include "room_scan/native_capture_pose.h"
#include <cstdio>
using namespace room_native;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
float prediction=0;std::atomic<unsigned> queries{0};
void query(ETrackingUniverseOrigin origin,float delta,TrackedDevicePose_t* out,std::uint32_t count){
    check(origin==ETrackingUniverseOrigin_TrackingUniverseStanding&&count==1,"standing head pose query");prediction=delta;++queries;
    out->bPoseIsValid=true;out->bDeviceIsConnected=true;out->eTrackingResult=ETrackingResult_TrackingResult_Running_OK;
    out->mDeviceToAbsoluteTracking=HmdMatrix34_t{{{1,0,0,1},{0,1,0,2},{0,0,1,3}}};
}
}
int main(){try{
    const auto center=ArcturusRectification::project({100,100,50,40},{0,0,0,0},{0,0,1});
    check(center[0]==50&&center[1]==40,"fisheye optical center");
    const auto diagonal=ArcturusRectification::project({100,100,50,40},{0,0,0,0},{1,0,1});
    check(std::abs(diagonal[0]-(50+100*std::atan(1.f)))<.001f&&diagonal[1]==40,"known 45 degree equidistant ray");
    check(ArcturusRectification::project({100,100,50,40},{0,0,0,0},{0,0,-1})[0]<0,"rear optical hemisphere is unavailable");
    VR_IVRSystem_FnTable system{};system.GetDeviceToAbsoluteTrackingPose=query;
    const auto target=rawClockNs()-30000000u;const auto value=capturePose(&system,target);
    check(prediction<-.029f&&prediction>-.034f&&value.targetRaw==target&&position(value.head).x==1,"capture time drives a negative history query");
    bool refused=false;try{capturePose(&system,rawClockNs()-100000000u);}catch(const std::exception&){refused=true;}
    check(refused,"outside history never silently falls back to current head pose");
    CapturePoseSpan span;const auto now=rawClockNs();span.read(&system,now-20000000u,now-10000000u);
    const auto middle=span.at(now-15000000u);
    check(rigid(middle)&&position(middle).x==1&&position(middle).y==2,"row interpolation preserves a fixed rigid tracked pose");
    refused=false;try{span.at(now);}catch(const std::exception&){refused=true;}
    check(refused,"rolling rows cannot extrapolate beyond observed poses");
    // Camera arrival may be 125 ms late. Use real earlier pose samples rather
    // than asking the runtime for unsupported history or requiring stillness.
    CapturePoseHistory history;const auto epoch=rawClockNs()-350000000u;
    for(unsigned i=0;i<=300;++i){const auto stamp=epoch+std::uint64_t(i)*1000000u;const float t=float(i)*.001f;
        const Mat head=pose({t*.15f,0,0},{0,std::sin(t*.3f/2),0,std::cos(t*.3f/2)});
        history.append({head,{.15f,0,0},{0,.3f,0},stamp,stamp-100,stamp+100});}
    const auto first=epoch+225000000u,last=first+10738680u;
    CapturePoseSpan delayed;delayed.read(nullptr,first,last,&history);
    const auto at=delayed.at(first+5000000u);
    check(std::abs(position(at).x-.23f*.15f)<1e-6f,"delayed camera rows use their actual moving capture-time position");
    check(delayed.rotationSpan>0&&delayed.translationSpan>0,"continuous movement does not require a stationary sample");
    check(delayed.queryBudget==200,"cached sample retains its actual API query bracket");
    const auto samples=delayed.exportSamples();check(samples.size()==9,"nine genuine sampled poses reach the PC");
    for(const auto& sample:samples)check((sample.at("raw_ns").get<std::uint64_t>()-epoch)%1000000u==0,"history exports observed timestamps, not fabricated query times");
    check(delayed.supportsTimeUncertainty(250000),"slow continuous movement supports a bounded exposure interval");
    check(!delayed.supportsTimeUncertainty(5000000),"time uncertainty cannot permit excessive spatial error");
    history.clear();refused=false;try{delayed.read(nullptr,first,last,&history);}catch(const std::exception&){refused=true;}
    check(refused,"cleared tracking history cannot borrow old room poses");
    for(unsigned i=0;i<10;++i){const auto stamp=epoch+std::uint64_t(i)*5000000u;
        history.append({Mat{},{},{},stamp,stamp-100,stamp+100});}
    std::array<CapturePose,9> sparse{};
    check(!history.span(epoch+5000000u,epoch+15000000u,sparse),"sparse pose samples do not silently interpolate across unobserved motion");
    const auto before=queries.load();history.start(&system);
    for(unsigned i=0;i<200&&queries==before;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    history.stop();const auto stopped=queries.load();
    check(stopped>before&&prediction==0,"owned sampler observes actual current public poses");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    check(queries==stopped&&!history.span(first,last,sparse),"Stop joins its sampler and clears tracking history");
    std::puts("PASS independent optical rays and capture-clock pose history bounds");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
