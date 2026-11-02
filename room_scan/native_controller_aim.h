// Resolve the runtime's controller tip; the tracked body's -Z is not its pointer.
#pragma once
#include <openvr_capi.h>
#include "native_math.h"
#include <optional>
#include <string>

namespace room_native {
class ControllerAim {
    struct Model {std::string name;double checked=-1000;};
    std::array<Model,64> models_{};
    VR_IVRSystem_FnTable* system_;VR_IVRRenderModels_FnTable* renderModels_;VR_IVRInput_FnTable* input_;
public:
    ControllerAim(VR_IVRSystem_FnTable* system,VR_IVRRenderModels_FnTable* models,VR_IVRInput_FnTable* input=nullptr)
        :system_(system),renderModels_(models),input_(input){}
    std::optional<Mat> resolve(unsigned device,const VRControllerState_t& input,const Mat& tracked,double now){
        if(device>=models_.size()||!rigid(tracked)||!std::isfinite(now)||!system_||!renderModels_||!system_->GetStringTrackedDeviceProperty)return {};
        auto& model=models_[device];
        if(now-model.checked>=2||now<model.checked){
            model.checked=now;model.name.clear();char name[256]{};ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
            const auto size=system_->GetStringTrackedDeviceProperty(device,ETrackedDeviceProperty_Prop_RenderModelName_String,name,sizeof(name),&error);
            if(error==ETrackedPropertyError_TrackedProp_Success&&size>1&&size<=sizeof(name)&&!name[size-1])model.name=name;}
        if(model.name.empty())return {};
        char tip[]="tip";RenderModel_ControllerMode_State_t mode{};RenderModel_ComponentState_t component{};bool found=false;
        if(input_&&input_->GetInputSourceHandle&&system_->GetControllerRoleForTrackedDeviceIndex&&renderModels_->GetComponentStateForDevicePath){
            const auto role=system_->GetControllerRoleForTrackedDeviceIndex(device);
            std::string path=role==ETrackedControllerRole_TrackedControllerRole_LeftHand?"/user/hand/left":
                role==ETrackedControllerRole_TrackedControllerRole_RightHand?"/user/hand/right":"";
            VRInputValueHandle_t handle=0;
            if(!path.empty()&&input_->GetInputSourceHandle(path.data(),&handle)==EVRInputError_VRInputError_None&&handle)
                found=renderModels_->GetComponentStateForDevicePath(model.name.data(),tip,handle,&mode,&component);}
        if(!found&&renderModels_->GetComponentState){auto current=input;
            found=renderModels_->GetComponentState(model.name.data(),tip,&current,&mode,&component);}
        if(!found)return {};
        const auto local=fromNative(component.mTrackingToComponentLocal);if(!rigid(local))return {};
        return multiply(tracked,local);
    }
};
inline bool hudBlocksRay(VR_IVROverlay_FnTable* overlay,VROverlayHandle_t hud,const Mat& aim){
    if(!overlay||!overlay->IsOverlayVisible||!overlay->ComputeOverlayIntersection||!hud||!rigid(aim)||!overlay->IsOverlayVisible(hud))return false;
    const auto source=position(aim),direction=column(aim,2)*-1;
    VROverlayIntersectionParams_t query{{{source.x,source.y,source.z}},{{direction.x,direction.y,direction.z}},ETrackingUniverseOrigin_TrackingUniverseStanding};
    VROverlayIntersectionResults_t result{};
    return overlay->ComputeOverlayIntersection(hud,&query,&result)&&std::isfinite(result.fDistance)&&result.fDistance>0;
}
} // namespace room_native
