// Close only Room Setup by selecting Steam Frame's existing SteamVR menu.
// This uses the public overlay API and never hides the complete dashboard.
#pragma once
#include <openvr_capi.h>

namespace room_native {
class DashboardControl {
    VR_IVROverlay_FnTable* overlay_=nullptr;VROverlayHandle_t panel_=0;
public:
    DashboardControl(VR_IVROverlay_FnTable* overlay,VROverlayHandle_t panel):overlay_(overlay),panel_(panel){}
    bool requestClose(){
        if(!overlay_||!overlay_->IsDashboardVisible||!overlay_->FindOverlay||!overlay_->ShowDashboard)return false;
        // Closing the small in-world controls must not open a hidden dashboard.
        if(!overlay_->IsDashboardVisible())return true;
        char key[]="valve.steam.gamepadui.main";VROverlayHandle_t menu=0;
        if(overlay_->FindOverlay(key,&menu)||!menu||menu==panel_)return false;
        overlay_->ShowDashboard(key);return true;
    }
};
} // namespace room_native
