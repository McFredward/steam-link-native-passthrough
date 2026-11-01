// Application metadata is optional for a service-owned dashboard. A parser or
// identification failure must not prevent CreateDashboardOverlay from running.
#pragma once
#include <openvr_capi.h>
#include <cstdint>
#include <string>

namespace room_native {
struct DashboardRegistration {
    bool manifestAdded=false,installed=false;
    int manifestError=-1,identityError=-1;
    EVROverlayError overlayError=EVROverlayError_VROverlayError_None;
};
inline DashboardRegistration createRoomDashboard(VR_IVRApplications_FnTable* applications,
    VR_IVROverlay_FnTable* overlay,const std::string& manifest,std::uint32_t pid,
    VROverlayHandle_t& panel,VROverlayHandle_t& thumbnail){
    DashboardRegistration result;char key[]="sfnp.room.setup",name[]="Room Setup";
    if(applications&&applications->AddApplicationManifest&&applications->IsApplicationInstalled){
        result.manifestError=int(applications->AddApplicationManifest(const_cast<char*>(manifest.c_str()),true));
        result.manifestAdded=result.manifestError==0;
        if(result.manifestAdded){
            result.installed=applications->IsApplicationInstalled(key);
            if(result.installed&&applications->IdentifyApplication)
                result.identityError=int(applications->IdentifyApplication(pid,key));
        }
    }
    result.overlayError=overlay->CreateDashboardOverlay(key,name,&panel,&thumbnail);
    return result;
}
} // namespace room_native
