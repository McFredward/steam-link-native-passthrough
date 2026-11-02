#include "room_scan/native_arcturus_geometry.h"
#include "room_scan/native_capture_pose.h"
#include <cstdio>
using namespace room_native;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
float prediction=0;
void query(ETrackingUniverseOrigin origin,float delta,TrackedDevicePose_t* out,std::uint32_t count){
    check(origin==ETrackingUniverseOrigin_TrackingUniverseStanding&&count==1,"standing head pose query");prediction=delta;
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
    std::puts("PASS independent optical rays and capture-clock pose history bounds");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
