// Application metadata is optional for a service-owned dashboard. A parser or
// identification failure must not prevent CreateDashboardOverlay from running.
#pragma once
#include <openvr_capi.h>
#include <cstdint>
#include <string>

namespace room_native {
#ifndef SFNP_ROOM_APP_KEY
#define SFNP_ROOM_APP_KEY "sfnp.room.setup"
#endif
#ifndef SFNP_ROOM_APP_NAME
#define SFNP_ROOM_APP_NAME "Room Setup"
#endif
inline constexpr const char* RoomApplicationKey=SFNP_ROOM_APP_KEY;
inline bool roomQuitRequested(const VREvent_t& event){
    // ProcessQuit announces another client's exit; it is not our quit request.
    return event.eventType==EVREventType_VREvent_Quit;
}
struct DashboardRegistration {
    bool manifestAdded=false,installed=false;
    int manifestError=-1,identityError=-1;
    EVROverlayError overlayError=EVROverlayError_VROverlayError_None;
};
inline DashboardRegistration createRoomDashboard(VR_IVRApplications_FnTable* applications,
    VR_IVROverlay_FnTable* overlay,const std::string& manifest,std::uint32_t pid,
    VROverlayHandle_t& panel,VROverlayHandle_t& thumbnail){
    DashboardRegistration result;std::string key=RoomApplicationKey,name=SFNP_ROOM_APP_NAME;
    if(applications&&applications->AddApplicationManifest&&applications->IsApplicationInstalled){
        result.manifestError=int(applications->AddApplicationManifest(const_cast<char*>(manifest.c_str()),true));
        result.manifestAdded=result.manifestError==0;
        if(result.manifestAdded){
            result.installed=applications->IsApplicationInstalled(key.data());
            if(result.installed&&applications->IdentifyApplication)
                result.identityError=int(applications->IdentifyApplication(pid,key.data()));
        }
    }
    result.overlayError=overlay->CreateDashboardOverlay(key.data(),name.data(),&panel,&thumbnail);
    return result;
}
} // namespace room_native
