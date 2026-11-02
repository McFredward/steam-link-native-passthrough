// Public overlay callbacks: close only this panel and preserve dashboard state.
#include "room_scan/native_dashboard.h"
#include "room_scan/native_registration.h"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace room_native;
namespace {
bool visible=true;unsigned shows=0;VROverlayHandle_t target=99;int lookupError=0;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
bool dashboardVisible(){return visible;}
EVROverlayError find(char* key,VROverlayHandle_t* result){
    check(std::string(key)=="valve.steam.gamepadui.main","return to the existing Frame SteamVR menu");
    *result=target;return EVROverlayError(lookupError);
}
void show(char* key){
    check(std::string(key)=="valve.steam.gamepadui.main","select the SteamVR menu instead of closing the dashboard");
    ++shows;visible=true;
}
}
int main(){try{
    VREvent_t event{};event.eventType=EVREventType_VREvent_ProcessQuit;event.data.process.pid=12345;
    check(!roomQuitRequested(event),"a departing helper client does not terminate Room Setup");
    event.eventType=EVREventType_VREvent_Quit;check(roomQuitRequested(event),"actual runtime quit is retained");
    VR_IVROverlay_FnTable api{};api.IsDashboardVisible=dashboardVisible;api.FindOverlay=find;api.ShowDashboard=show;
    DashboardControl control(&api,42);
    check(control.requestClose()&&visible&&shows==1,"closing Room Setup preserves the open SteamVR dashboard");
    visible=false;check(control.requestClose()&&!visible&&shows==1,"closing in-world controls preserves a hidden dashboard");
    visible=true;lookupError=1;check(!control.requestClose()&&shows==1&&visible,"missing menu never closes the dashboard");
    lookupError=0;target=42;check(!control.requestClose()&&shows==1,"cannot select Room Setup as its own close target");
    check(!DashboardControl(nullptr,42).requestClose(),"missing interfaces are refused safely");
    std::puts("PASS room-only close, preserved SteamVR dashboard and ProcessQuit isolation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
