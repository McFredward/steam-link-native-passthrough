// Focused native aim/grip and select actions. No tracked-body axis assumptions.
#pragma once
#include "native_xr_api.h"
#include "native_scene_protocol.h"
#include <cstring>

namespace room_xr {
class SceneInput {
    XrActionSet set_=XR_NULL_HANDLE;XrAction aim_=XR_NULL_HANDLE,grip_=XR_NULL_HANDLE,click_=XR_NULL_HANDLE,trigger_=XR_NULL_HANDLE;
    std::array<XrPath,2> hands_{};std::array<XrSpace,2> aims_{},grips_{};XrInstance instance_=XR_NULL_HANDLE;
    static void check(XrResult result){if(XR_FAILED(result))throw std::runtime_error("Native room actions unavailable");}
    XrPath path(const std::string& text){XrPath result=XR_NULL_PATH;check(xrStringToPath(instance_,text.c_str(),&result));return result;}
    XrAction action(const char* name,XrActionType type){
        XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};std::strcpy(info.actionName,name);std::strcpy(info.localizedActionName,name);
        info.actionType=type;info.countSubactionPaths=2;info.subactionPaths=hands_.data();XrAction result=XR_NULL_HANDLE;
        check(xrCreateAction(set_,&info,&result));return result;
    }
    static room_native::Mat matrix(const XrPosef& pose){return room_native::pose({pose.position.x,pose.position.y,pose.position.z},
        {pose.orientation.x,pose.orientation.y,pose.orientation.z,pose.orientation.w});}
public:
    void initialize(XrInstance instance,XrSession session){
        instance_=instance;hands_={path("/user/hand/left"),path("/user/hand/right")};
        XrActionSetCreateInfo info{XR_TYPE_ACTION_SET_CREATE_INFO};std::strcpy(info.actionSetName,"room_authoring");std::strcpy(info.localizedActionSetName,"Room tools");
        check(xrCreateActionSet(instance_,&info,&set_));aim_=action("aim",XR_ACTION_TYPE_POSE_INPUT);grip_=action("grip",XR_ACTION_TYPE_POSE_INPUT);
        click_=action("select",XR_ACTION_TYPE_BOOLEAN_INPUT);trigger_=action("trigger",XR_ACTION_TYPE_FLOAT_INPUT);
        for(const auto* profile:{"/interaction_profiles/khr/simple_controller","/interaction_profiles/valve/index_controller","/interaction_profiles/oculus/touch_controller"}){
            const bool simple=std::strstr(profile,"simple_controller")!=nullptr;std::vector<XrActionSuggestedBinding> bindings;
            for(const auto* hand:{"/user/hand/left","/user/hand/right"}){
                const std::string base=hand;bindings.push_back({aim_,path(base+"/input/aim/pose")});bindings.push_back({grip_,path(base+"/input/grip/pose")});
                bindings.push_back({simple?click_:trigger_,path(base+(simple?"/input/select/click":"/input/trigger/value"))});}
            XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggested.interactionProfile=path(profile);
            suggested.countSuggestedBindings=bindings.size();suggested.suggestedBindings=bindings.data();const auto result=xrSuggestInteractionProfileBindings(instance_,&suggested);
            if(simple)check(result);
        }
        for(unsigned i=0;i<2;++i)for(unsigned kind=0;kind<2;++kind){XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            space.action=kind?grip_:aim_;space.subactionPath=hands_[i];space.poseInActionSpace.orientation.w=1;
            check(xrCreateActionSpace(session,&space,kind?&grips_[i]:&aims_[i]));}
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&set_;
        check(xrAttachSessionActionSets(session,&attach));
    }
    nlohmann::json sample(XrSession session,XrSpace stage,XrTime time,bool focused){
        nlohmann::json hands=nlohmann::json::array();if(!focused)return hands;
        XrActiveActionSet active{set_,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
        if(xrSyncActions(session,&sync)!=XR_SUCCESS)return hands;
        for(unsigned i=0;i<2;++i){
            XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=hands_[i];get.action=aim_;
            XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};if(XR_FAILED(xrGetActionStatePose(session,&get,&state))||!state.isActive)continue;
            XrSpaceLocation aim{XR_TYPE_SPACE_LOCATION},grip{XR_TYPE_SPACE_LOCATION};
            const auto flags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            if(XR_FAILED(xrLocateSpace(aims_[i],stage,time,&aim))||(aim.locationFlags&flags)!=flags)continue;
            if(XR_FAILED(xrLocateSpace(grips_[i],stage,time,&grip))||(grip.locationFlags&flags)!=flags)continue;
            get.action=click_;XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN};check(xrGetActionStateBoolean(session,&get,&click));
            get.action=trigger_;XrActionStateFloat trigger{XR_TYPE_ACTION_STATE_FLOAT};check(xrGetActionStateFloat(session,&get,&trigger));
            hands.push_back({{"hand",i},{"aim",room_native::sceneMatrix(matrix(aim.pose))},{"grip",room_native::sceneMatrix(matrix(grip.pose))},
                {"pressed",(click.isActive&&click.currentState)||(trigger.isActive&&trigger.currentState>.55f)}});
        }
        return hands;
    }
    void close(){for(auto& spaces:{&aims_,&grips_})for(auto& space:*spaces)if(space){xrDestroySpace(space);space=XR_NULL_HANDLE;}
        if(set_){xrDestroyActionSet(set_);set_=XR_NULL_HANDLE;}}
};
} // namespace room_xr
