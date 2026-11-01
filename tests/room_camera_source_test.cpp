// Synthetic public function tables: no hardware, files or real image capture.
#include "room_scan/native_camera.h"
#include <cstdio>
using namespace room_native;
namespace {
struct Fixture {
    bool camera=true;int count=2,layout=34;
    int acquires=0,releases=0,layoutCalls=0;
    EVRTrackedCameraError cameraError=EVRTrackedCameraError_VRTrackedCameraError_None;
    ETrackedPropertyError countError=ETrackedPropertyError_TrackedProp_Success;
    bool invalidIntrinsics=false,metadataAvailable=true;float separation=.064f;
} fixture;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::array<Mat,2> geometry(){std::array<Mat,2> m{};m[0].m[0][3]=-fixture.separation/2;m[1].m[0][3]=fixture.separation/2;return m;}
EVRTrackedCameraError has(TrackedDeviceIndex_t,bool* value){*value=fixture.camera;return fixture.cameraError;}
std::int32_t property(TrackedDeviceIndex_t,ETrackedDeviceProperty prop,ETrackedPropertyError* error){
    if(prop==ETrackedDeviceProperty_Prop_NumCameras_Int32){*error=fixture.countError;return fixture.count;}
    ++fixture.layoutCalls;*error=ETrackedPropertyError_TrackedProp_Success;return fixture.metadataAvailable?fixture.layout:0;
}
std::uint32_t array(TrackedDeviceIndex_t,ETrackedDeviceProperty,PropertyTypeTag_t,void* bytes,std::uint32_t size,ETrackedPropertyError* error){
    const auto matrices=geometry();check(size==sizeof(matrices),"bounded public extrinsic extent");
    if(!fixture.metadataAvailable){*error=ETrackedPropertyError_TrackedProp_UnknownProperty;return 0;}
    std::memcpy(bytes,matrices.data(),size);*error=ETrackedPropertyError_TrackedProp_Success;return size;
}
EVRTrackedCameraError frameSize(TrackedDeviceIndex_t,EVRTrackedCameraFrameType,std::uint32_t* w,std::uint32_t* h,std::uint32_t* bytes){
    if(!fixture.metadataAvailable)return EVRTrackedCameraError_VRTrackedCameraError_OperationFailed;
    *w=640;*h=240;*bytes=(*w)*(*h)*4;return EVRTrackedCameraError_VRTrackedCameraError_None;
}
EVRTrackedCameraError intrinsics(TrackedDeviceIndex_t,std::uint32_t,EVRTrackedCameraFrameType,HmdVector2_t* f,HmdVector2_t* c){
    if(!fixture.metadataAvailable)return EVRTrackedCameraError_VRTrackedCameraError_OperationFailed;
    *f=HmdVector2_t{{fixture.invalidIntrinsics?0.f:220.f,220.f}};*c=HmdVector2_t{{159.5f,119.5f}};
    return EVRTrackedCameraError_VRTrackedCameraError_None;
}
EVRTrackedCameraError acquire(TrackedDeviceIndex_t,TrackedCameraHandle_t* handle){++fixture.acquires;*handle=42;return EVRTrackedCameraError_VRTrackedCameraError_None;}
EVRTrackedCameraError release(TrackedCameraHandle_t){++fixture.releases;return EVRTrackedCameraError_VRTrackedCameraError_None;}
EVRTrackedCameraError buffer(TrackedCameraHandle_t,EVRTrackedCameraFrameType,void*,std::uint32_t,CameraVideoStreamFrameHeader_t*,std::uint32_t){return EVRTrackedCameraError_VRTrackedCameraError_NoFrameAvailable;}
ETrackingUniverseOrigin space(){return ETrackingUniverseOrigin_TrackingUniverseStanding;}
}
int main(){try{
    VR_IVRSystem_FnTable system{};system.GetInt32TrackedDeviceProperty=property;system.GetArrayTrackedDeviceProperty=array;
    VR_IVRTrackedCamera_FnTable api{};api.HasCamera=has;api.GetCameraFrameSize=frameSize;api.GetCameraIntrinsics=intrinsics;
    api.AcquireVideoStreamingService=acquire;api.ReleaseVideoStreamingService=release;api.GetVideoStreamFrameBuffer=buffer;api.GetCameraTrackingSpace=space;
    StereoCamera camera;camera.system=&system;camera.factoryGeometry=geometry;
    check(!camera.start("session","origin")&&camera.status=="public-camera-api-unavailable","missing public interface has a precise failure");
    camera.api=&api;fixture.camera=false;
    check(!camera.start("session","origin")&&camera.status=="camera-not-exposed"&&fixture.acquires==0,"no exposed camera cannot acquire images");
    fixture=Fixture{};fixture.count=6;
    check(!camera.start("session","origin")&&camera.status=="camera-count-unsupported"&&fixture.layoutCalls==0&&fixture.acquires==0,"unsupported camera count stops before acquisition");
    check(camera.diagnostics.at("camera_count")==6&&camera.diagnostics.at("check")=="camera-count","journal diagnostics retain the failing gate and actual count");
    fixture=Fixture{};fixture.count=0;fixture.metadataAvailable=false;
    check(!camera.start("session","origin")&&camera.status=="camera-metadata-unavailable"&&fixture.acquires==0,"Frame's zero count is missing API metadata, not an unsupported physical camera");
    check(camera.diagnostics.at("formats").size()==3&&camera.diagnostics.at("arcturus_factory_geometry_readable")==true,"zero-count inventory checks actual formats and factory availability without acquiring");
    fixture=Fixture{};fixture.count=0;
    check(camera.start("session","origin")&&fixture.acquires==1,"real stereo format, exactly two factory-matched extrinsics and valid intrinsics can establish stereo independently of a missing count");
    camera.stop();
    fixture=Fixture{};fixture.count=0;fixture.invalidIntrinsics=true;
    check(!camera.start("session","origin")&&fixture.acquires==0,"zero-count metadata never bypasses intrinsic validation");
    fixture=Fixture{};fixture.countError=ETrackedPropertyError_TrackedProp_UnknownProperty;
    check(!camera.start("session","origin")&&camera.status=="camera-count-query-failed"&&fixture.acquires==0,"a property error is distinct from a mismatching count");
    fixture=Fixture{};fixture.layout=0;
    check(!camera.start("session","origin")&&camera.status=="camera-layout-unsupported"&&fixture.acquires==0,"unknown stereo layout cannot acquire images");
    fixture=Fixture{};fixture.invalidIntrinsics=true;
    check(!camera.start("session","origin")&&camera.status=="camera-intrinsics-invalid"&&fixture.acquires==0,"invalid focal length cannot acquire images");
    fixture=Fixture{};fixture.separation=.2f;
    check(!camera.start("session","origin")&&camera.status=="camera-baseline-unsupported"&&fixture.acquires==0,"invalid metric scale cannot acquire images");
    fixture=Fixture{};camera.factoryGeometry=[]()->std::array<Mat,2>{throw std::runtime_error("fixture calibration failure");};
    check(!camera.start("session","origin")&&camera.status=="camera-calibration-unverified"&&fixture.acquires==0,"calibration exceptions retain their stage without exposing file contents");
    check(camera.diagnostics.at("check")=="arcturus-factory-calibration","calibration failure keeps its exact gate");
    fixture=Fixture{};camera.factoryGeometry=geometry;
    check(camera.start("session","origin")&&fixture.acquires==1&&camera.handle==42,"verified synthetic stereo reaches acquisition once");
    check(camera.diagnostics.at("check")=="ready","successful gate diagnostics are explicit");
    check(camera.next().empty(),"unavailable real observation never creates synthetic depth");
    camera.stop();check(fixture.releases==1&&!camera.handle,"owned camera acquisition is released once");
    std::puts("PASS precise public camera gates, refusal before acquisition and owned cleanup");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
