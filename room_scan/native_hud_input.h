// SteamVR owns the in-room menu's pointer, surface intersection and mouse input.
#pragma once
#include <openvr_capi.h>
#include <memory>
#include "native_present.h"
#include "native_view.h"

namespace room_native {
inline std::string hudPixelAction(const Json& state,float x,float y){
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>1024||y<0||y>240)return {};
    if(hitClose(x,y))return "close";
    if(y<145||y>215)return {};
    for(std::size_t i=0;i<std::min<std::size_t>(3,state.at("buttons").size());++i){const auto& b=state.at("buttons")[i];
        if(b.value("enabled",false)&&x>=20+i*334&&x<=334+i*334)return b.value("action","");}
    return {};
}
class RoomHudInput {
    std::string pressed_,hovered_;std::uint32_t cursor_=0;
    std::shared_ptr<const Json> pressedState_;
public:
    static bool configure(VR_IVROverlay_FnTable* overlay,VROverlayHandle_t handle){
        if(!overlay||!handle||!overlay->SetOverlayInputMethod||!overlay->SetOverlayMouseScale||!overlay->SetOverlayFlag)return false;
        HmdVector2_t scale{{1024,240}};
        return overlay->SetOverlayMouseScale(handle,&scale)==EVROverlayError_VROverlayError_None
            &&overlay->SetOverlayInputMethod(handle,VROverlayInputMethod_Mouse)==EVROverlayError_VROverlayError_None
            &&overlay->SetOverlayFlag(handle,VROverlayFlags_MakeOverlaysInteractiveIfVisible,true)==EVROverlayError_VROverlayError_None;
    }
    const std::string& hovered()const{return hovered_;}
    void reset(){pressed_.clear();hovered_.clear();pressedState_.reset();}
    std::string event(const VREvent_t& event,std::shared_ptr<const Json> displayed,const Json& current,bool visible){
        if(!visible||!displayed||!sameControls(*displayed,current)
            ||event.eventType==EVREventType_VREvent_FocusLeave||event.eventType==EVREventType_VREvent_OverlayHidden){reset();return {};}
        if(event.eventType!=EVREventType_VREvent_MouseMove&&event.eventType!=EVREventType_VREvent_MouseButtonDown
            &&event.eventType!=EVREventType_VREvent_MouseButtonUp)return {};
        const auto hit=hudPixelAction(*displayed,event.data.mouse.x,240-event.data.mouse.y);
        hovered_=hit;
        if(event.eventType==EVREventType_VREvent_MouseButtonDown&&event.data.mouse.button==EVRMouseButton_VRMouseButton_Left){
            pressed_=hit;pressedState_=std::move(displayed);cursor_=event.data.mouse.cursorIndex;}
        if(event.eventType==EVREventType_VREvent_MouseButtonUp&&event.data.mouse.button==EVRMouseButton_VRMouseButton_Left
            &&event.data.mouse.cursorIndex==cursor_){
            const bool activate=!hit.empty()&&hit==pressed_&&pressedState_&&sameControls(*pressedState_,current);
            pressed_.clear();pressedState_.reset();if(activate)return hit;}
        return {};
    }
};
} // namespace room_native
