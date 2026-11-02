// Regress the real compact-menu input path and runtime controller tip transform.
#include "room_scan/native_hud_input.h"
#include "room_scan/native_controller_aim.h"
#include <cstdio>
#include <cstring>
using namespace room_native;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
unsigned configured=0;bool failConfig=false,tipAvailable=true,overlayVisible=true;Mat localTip;unsigned tipRequests=0;
EVROverlayError scale(VROverlayHandle_t handle,HmdVector2_t* value){
    check(handle==17&&value->v[0]==1024&&value->v[1]==240,"native mouse coordinates match the displayed compact texture");++configured;return EVROverlayError_VROverlayError_None;}
EVROverlayError method(VROverlayHandle_t handle,VROverlayInputMethod value){
    check(handle==17&&value==VROverlayInputMethod_Mouse,"scan controls request native mouse input");++configured;return EVROverlayError_VROverlayError_None;}
EVROverlayError flag(VROverlayHandle_t handle,VROverlayFlags value,bool enabled){
    check(handle==17&&value==VROverlayFlags_MakeOverlaysInteractiveIfVisible&&enabled,"visible scan controls activate SteamVR's own laser mouse");
    ++configured;return failConfig?EVROverlayError_VROverlayError_InvalidParameter:EVROverlayError_VROverlayError_None;}
uint32_t model(unsigned,ETrackedDeviceProperty property,char* buffer,uint32_t capacity,ETrackedPropertyError* error){
    check(property==ETrackedDeviceProperty_Prop_RenderModelName_String&&capacity>=11,"resolve the actual controller render model");
    std::memcpy(buffer,"test_model",11);*error=ETrackedPropertyError_TrackedProp_Success;return 11;}
ETrackedControllerRole role(unsigned device){return device==1?ETrackedControllerRole_TrackedControllerRole_LeftHand:ETrackedControllerRole_TrackedControllerRole_RightHand;}
EVRInputError source(char* path,VRInputValueHandle_t* out){*out=std::strcmp(path,"/user/hand/left")==0?100:200;return EVRInputError_VRInputError_None;}
bool component(char* name,char* part,VRInputValueHandle_t path,RenderModel_ControllerMode_State_t*,RenderModel_ComponentState_t* out){
    check(std::string(name)=="test_model"&&std::string(part)=="tip"&&(path==100||path==200),"use the runtime tip component for the matching hand");
    ++tipRequests;if(!tipAvailable)return false;std::memcpy(&out->mTrackingToComponentLocal,&localTip,sizeof(localTip));return true;}
bool visible(VROverlayHandle_t handle){check(handle==17,"query the real controls overlay");return overlayVisible;}
bool intersection(VROverlayHandle_t handle,VROverlayIntersectionParams_t* query,VROverlayIntersectionResults_t* result){
    check(handle==17&&query->eOrigin==ETrackingUniverseOrigin_TrackingUniverseStanding,"menu intersections use the same standing origin as the measured room");
    const float z=query->vDirection.v[2];if(std::abs(z)<1e-6f)return false;
    const float distance=(-1-query->vSource.v[2])/z;if(distance<=0)return false;
    const float x=query->vSource.v[0]+distance*query->vDirection.v[0];
    if(std::abs(x)>.5f)return false;
    result->fDistance=distance;return true;}
VREvent_t mouse(uint32_t kind,float x,float y,uint32_t cursor=0){VREvent_t event{};event.eventType=kind;
    event.data.mouse={x,240-y,EVRMouseButton_VRMouseButton_Left,cursor};return event;}
}
int main(){try{
    VR_IVROverlay_FnTable overlay{};overlay.SetOverlayMouseScale=scale;overlay.SetOverlayInputMethod=method;overlay.SetOverlayFlag=flag;
    overlay.IsOverlayVisible=visible;overlay.ComputeOverlayIntersection=intersection;
    check(RoomHudInput::configure(&overlay,17)&&configured==3,"all native laser/mouse settings are required");
    failConfig=true;check(!RoomHudInput::configure(&overlay,17),"failed native input setup cannot silently start an unusable scan");
    auto state=std::make_shared<const Json>(Json{{"page","scan"},{"buttons",{{{"action","review"},{"enabled",true}},
        {{"action","add"},{"enabled",true}},{{"action","pause"},{"enabled",false}}}}});
    RoomHudInput input;
    check(input.event(mouse(EVREventType_VREvent_MouseMove,175,180),state,*state,true).empty()&&input.hovered()=="review","native pixel coordinates drive the same hover target as clicks");
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180),state,*state,true).empty(),"a trigger already held when opening the menu cannot activate a button on release");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,175,180),state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180),state,*state,true)=="review","SteamVR's button event activates the displayed scan control");
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180),state,*state,true).empty(),"one native click dispatches once");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,175,180),state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,510,180),state,*state,true).empty(),"dragging between buttons does not activate either one");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,844,180),state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,844,180),state,*state,true).empty(),"disabled controls remain disabled");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,964,52),state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,964,52),state,*state,true)=="close","X uses the actual native compact-menu coordinates");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,175,180),state,*state,true);
    VREvent_t leave{};leave.eventType=EVREventType_VREvent_FocusLeave;input.event(leave,state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180),state,*state,true).empty(),"losing pointer focus cancels the pending click");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,175,180,0),state,*state,true);
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180,1),state,*state,true).empty(),"another hand cannot release a captured click");
    auto changed=*state;changed["page"]="place";
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,175,180),state,changed,true).empty(),"a page transition cannot dispatch a stale button");
    input.event(mouse(EVREventType_VREvent_MouseButtonDown,964,52),state,*state,true);input.reset();
    check(input.event(mouse(EVREventType_VREvent_MouseButtonUp,964,52),state,*state,true).empty(),"reopening starts with no held click");
    VR_IVRSystem_FnTable system{};system.GetStringTrackedDeviceProperty=model;system.GetControllerRoleForTrackedDeviceIndex=role;
    VR_IVRRenderModels_FnTable models{};models.GetComponentStateForDevicePath=component;
    VR_IVRInput_FnTable vrInput{};vrInput.GetInputSourceHandle=source;ControllerAim aim(&system,&models,&vrInput);VRControllerState_t controller{};
    const float angle=.4f;localTip=pose({.03f,.04f,-.08f},{std::sin(angle/2),0,0,std::cos(angle/2)});
    const Mat tracked=pose({1,2,3},{0,std::sin(.3f),0,std::cos(.3f)});const auto resolved=aim.resolve(1,controller,tracked,0);
    check(bool(resolved)&&tipRequests==1,"a runtime tip is required for world tools");
    const auto expectedPosition=transform(tracked,{.03f,.04f,-.08f});
    const auto expectedDirection=column(tracked,1)*std::sin(angle)-column(tracked,2)*std::cos(angle);
    check(length(position(*resolved)-expectedPosition)<1e-5f&&length(column(*resolved,2)*-1-expectedDirection)<1e-5f,"tip translation and angle are composed with the tracked controller rather than using its body axis");
    check(hudBlocksRay(&overlay,17,Mat{}),"an interactive menu surface blocks authoring rays before room geometry");
    Mat away;away.m[0][3]=2;check(!hudBlocksRay(&overlay,17,away),"world tools remain available away from the menu");
    overlayVisible=false;check(!hudBlocksRay(&overlay,17,Mat{}),"a hidden menu cannot intercept the world ray");
    tipAvailable=false;check(!aim.resolve(2,controller,Mat{},0),"missing tip data does not invent a controller angle");
    std::puts("PASS native scan-menu mouse events, hover, single dispatch, close/reopen, stale-click refusal, runtime tip and menu ray interception");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
