// Projection layers take a tracking-origin-to-eye VIEW, not an absolute pose.
#pragma once
#include <openvr_capi.h>
#include "native_math.h"
#include <cstdint>
#include <cstring>

namespace room_native {
struct RoomDisplayPrediction {float seconds=0,period=0;bool verified=false;};
inline RoomDisplayPrediction roomDisplayPrediction(VR_IVRSystem_FnTable* system){
    // This prediction belongs only to display rendering. Exposure-time camera
    // reconstruction continues to use its independently verified history.
    if(!system||!system->GetFloatTrackedDeviceProperty||!system->GetTimeSinceLastVsync)return {};
    ETrackedPropertyError frequencyError=ETrackedPropertyError_TrackedProp_Success,photonsError=frequencyError;
    const float frequency=system->GetFloatTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_DisplayFrequency_Float,&frequencyError);
    const float photons=system->GetFloatTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_SecondsFromVsyncToPhotons_Float,&photonsError);
    float since=0;std::uint64_t counter=0;
    if(frequencyError||photonsError||!system->GetTimeSinceLastVsync(&since,&counter)
        ||!std::isfinite(frequency)||frequency<30||frequency>240||!std::isfinite(photons)||photons<0||photons>.05f
        ||!std::isfinite(since)||since<0||since>1.5f/frequency)return {};
    const float delay=1/frequency-since+photons;
    if(delay<0||delay>.075f)return {};
    return {delay,1/frequency,true};
}
inline Frustum roomProjectionFrustum(VR_IVRSystem_FnTable* system,unsigned eye){
    if(!system||!system->GetProjectionRaw||!system->GetProjectionMatrix||eye>1)
        throw std::runtime_error("Room projection calibration is unavailable");
    Frustum f;system->GetProjectionRaw(EVREye(eye),&f.left,&f.right,&f.top,&f.bottom);
    if(!std::isfinite(f.left)||!std::isfinite(f.right)||!std::isfinite(f.top)||!std::isfinite(f.bottom)
        ||f.left>=f.right||f.top>=f.bottom)throw std::runtime_error("Invalid room projection frustum");
    const auto native=system->GetProjectionMatrix(EVREye(eye),.1f,10.f);
    for(V3 point:{V3{0,0,-1},V3{.4f,.5f,-2},V3{-.3f,-.2f,-1.5f}}){
        float x=0,y=0;if(!project(point,f,1024,1024,x,y))throw std::runtime_error("Invalid room projection ray");
        const std::array<float,4> v{point.x,point.y,point.z,1};std::array<float,4> clip{};
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)clip[r]+=native.m[r][c]*v[c];
        if(!std::isfinite(clip[0])||!std::isfinite(clip[1])||!std::isfinite(clip[3])||clip[3]<=0
            ||std::abs((1+clip[0]/clip[3])*512-x)>.05f||std::abs((1-clip[1]/clip[3])*512-y)>.05f)
            throw std::runtime_error("Room viewport differs from the native eye projection");
    }
    return f;
}
inline EVROverlayError setRoomProjection(VR_IVROverlay_FnTable* overlay,VROverlayHandle_t handle,
        const Mat& roomFromEye,const Frustum& frustum,unsigned eye){
    if(!overlay||!overlay->SetOverlayTransformProjection||!rigid(roomFromEye)||eye>1
        ||!std::isfinite(frustum.left)||!std::isfinite(frustum.right)
        ||!std::isfinite(frustum.top)||!std::isfinite(frustum.bottom)
        ||frustum.left>=frustum.right||frustum.top>=frustum.bottom)
        throw std::runtime_error("Invalid room projection view");
    const Mat eyeFromRoom=inverse(roomFromEye);
    HmdMatrix34_t view{};std::memcpy(&view,&eyeFromRoom,sizeof(Mat));
    VROverlayProjection_t shape{frustum.left,frustum.right,frustum.top,frustum.bottom};
    return overlay->SetOverlayTransformProjection(handle,ETrackingUniverseOrigin_TrackingUniverseStanding,
        &view,&shape,EVREye(eye));
}
} // namespace room_native
