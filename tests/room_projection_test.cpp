#include "room_scan/native_projection.h"
#include "room_scan/native_scan_view.h"
#include <cstdio>
using namespace room_native;
namespace {
Mat published;unsigned calls=0;
void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
const std::array<Frustum,2> frameFrustums{{
    {-1.6665174961f,1.2107580900f,-1.7513343096f,1.1571623087f},
    {-1.2063491344f,1.6771156788f,-1.7136186361f,1.1824873686f}}};
bool corruptMatrix=false;
bool invalidTiming=false;
bool displayClock(float* since,std::uint64_t* counter){*since=.004f;*counter=73;return true;}
float displayProperty(TrackedDeviceIndex_t,ETrackedDeviceProperty property,ETrackedPropertyError* error){
    *error=invalidTiming?ETrackedPropertyError_TrackedProp_UnknownProperty:ETrackedPropertyError_TrackedProp_Success;
    return property==ETrackedDeviceProperty_Prop_DisplayFrequency_Float?90.f:.006f;
}
void frameRaw(EVREye eye,float* l,float* r,float* t,float* b){
    const auto f=frameFrustums.at(unsigned(eye));*l=f.left;*r=f.right;*t=f.top;*b=f.bottom;
}
HmdMatrix44_t frameMatrix(EVREye eye,float,float){
    // Independently observed public-API matrices from the Frame. Do not derive
    // these coefficients from the raster formula under test.
    HmdMatrix44_t matrix{};
    matrix.m[0][0]=eye==EVREye_Eye_Left?.6951020360f:.6936100125f;
    matrix.m[0][2]=eye==EVREye_Eye_Left?-.1583996415f:.1632642001f;
    matrix.m[1][1]=eye==EVREye_Eye_Left?.6876404881f:.6905824542f;
    matrix.m[1][2]=eye==EVREye_Eye_Left?-.2042883635f:-.1833949685f;
    if(corruptMatrix)matrix.m[1][2]=0;
    matrix.m[2][2]=-1.01010108f;matrix.m[2][3]=-.101010107f;matrix.m[3][2]=-1;
    return matrix;
}
EVROverlayError projection(VROverlayHandle_t handle,ETrackingUniverseOrigin origin,HmdMatrix34_t* view,VROverlayProjection_t* f,EVREye eye){
    check(handle==37&&origin==ETrackingUniverseOrigin_TrackingUniverseStanding&&eye==EVREye_Eye_Right,"standing right-eye layer");
    check(f->fLeft==-.9f&&f->fRight==1.2f&&f->fTop==-1.1f&&f->fBottom==1.3f,"asymmetric optical frustum preserved");
    published=fromNative(*view);++calls;return EVROverlayError_VROverlayError_None;
}
}
int main(){try{
    VR_IVRSystem_FnTable system{};system.GetProjectionRaw=frameRaw;system.GetProjectionMatrix=frameMatrix;
    system.GetTimeSinceLastVsync=displayClock;system.GetFloatTrackedDeviceProperty=displayProperty;
    const auto prediction=roomDisplayPrediction(&system);
    check(prediction.verified&&std::abs(prediction.seconds-(1.f/90-.004f+.006f))<1e-6f,"display-time prediction follows the runtime clock and photon delay");
    invalidTiming=true;const auto absent=roomDisplayPrediction(&system);
    check(!absent.verified&&absent.seconds==0,"missing display timing cannot invent a fixed prediction");invalidTiming=false;
    for(unsigned eye=0;eye<2;++eye){
        const auto optical=roomProjectionFrustum(&system,eye);
        float x=0,y=0;check(project({0,0,-1},optical,1024,1024,x,y),"Frame optical axis projects");
        const float expected=eye?418.101776f:407.404358f;
        check(std::abs(y-expected)<.001f,"Frame asymmetric optical centre uses the native vertical convention");
    }
    corruptMatrix=true;bool refused=false;
    try{roomProjectionFrustum(&system,0);}catch(const std::runtime_error&){refused=true;}
    check(refused,"inconsistent native calibration refuses world rendering");corruptMatrix=false;
    VR_IVROverlay_FnTable api{};api.SetOverlayTransformProjection=projection;
    const Frustum f{-.9f,1.2f,-1.1f,1.3f};
    const V3 landmark{.4f,1.1f,-2.3f};unsigned wrong=0;
    for(float yaw:{-.7f,-.3f,0.f,.2f,.8f})for(float x:{-.2f,.0f,.3f}){
        const Mat eye=pose({x,1.5f,.1f},{0,std::sin(yaw/2),0,std::cos(yaw/2)});
        check(setRoomProjection(&api,37,eye,f,1)==0,"projection accepted");
        const V3 delta=landmark-position(eye);
        const V3 independent{dot(column(eye,0),delta),dot(column(eye,1),delta),dot(column(eye,2),delta)};
        check(length(transform(published,landmark)-independent)<1e-5f,"view locates a fixed landmark under rotation and translation");
        float u=0,v=0;check(project(independent,f,768,768,u,v),"landmark is in front");
        const V3 ray{f.left+u/768*(f.right-f.left),f.bottom-v/768*(f.bottom-f.top),-1};
        const Mat roomFromPublished=inverse(published);
        const V3 recovered=transform(roomFromPublished,ray*(-independent.z));
        check(length(recovered-landmark)<1e-5f,"compositor ray returns to the same world landmark");
        if(length(transform(eye,landmark)-independent)>.1f)++wrong;
    }
    check(calls==15&&wrong==15,"the former absolute-pose publication fails the independent landmark check");
    // Independent native projection-matrix convention, including unequal top
    // and bottom tangents. A symmetric test frustum hid the old vertical bug.
    float x=0,y=0;check(project({0,0,-1},f,768,768,x,y),"optical axis projects");
    const float matrixY=.5f*(1+(f.bottom+f.top)/(f.bottom-f.top))*768;
    check(std::abs(y-matrixY)<.0001f,"asymmetric eye centre agrees with native projection-matrix clip coordinates");
    ScanViewTransition flow;
    check(!flow.observe({{"page","prepare"},{"capture_collecting",false}}),"camera preparation keeps its dashboard");
    check(flow.observe({{"page","scan"},{"capture_collecting",true}}),"explicit capture enters an unobstructed room view");
    check(!flow.observe({{"page","scan"},{"capture_collecting",true}}),"opening the menu during capture is not fought every frame");
    check(!flow.observe({{"page","types"},{"capture_collecting",true}}),"the element menu remains visible");
    check(flow.observe({{"page","place"},{"capture_collecting",true}}),"corner measurement returns to the room view");
    check(!flow.observe({{"page","place"},{"capture_collecting",true}}),"repeated corner updates do not repeat the dashboard command");
    std::puts("PASS projection view preserves world landmarks under head rotation and translation");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
