// Startup regression: optional manifest metadata cannot suppress the dashboard.
#include "room_scan/native_registration.h"
#include <cstdio>
#include <stdexcept>
using namespace room_native;
namespace {
bool installed=false;int adds=0,identities=0,dashboards=0,manifestCode=0,identityCode=0;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
EVRApplicationError add(char*,bool temporary){check(temporary,"registration is session-owned");++adds;return EVRApplicationError(manifestCode);}
bool has(char*){return installed;}
EVRApplicationError identify(std::uint32_t,char*){++identities;return EVRApplicationError(identityCode);}
EVROverlayError create(char* key,char* name,VROverlayHandle_t* panel,VROverlayHandle_t* thumbnail){
    check(std::string(key)=="sfnp.room.setup"&&std::string(name)=="Room Setup","stable overlay key and English dashboard title");
    ++dashboards;*panel=42;*thumbnail=43;return EVROverlayError_VROverlayError_None;
}
}
int main(){try{
    VR_IVRApplications_FnTable applications{};applications.AddApplicationManifest=add;applications.IsApplicationInstalled=has;applications.IdentifyApplication=identify;
    VR_IVROverlay_FnTable overlay{};overlay.CreateDashboardOverlay=create;
    VROverlayHandle_t panel=0,thumbnail=0;
    auto r=createRoomDashboard(&applications,&overlay,"fixture.vrmanifest",10,panel,thumbnail);
    check(r.manifestAdded&&!r.installed&&dashboards==1&&panel==42&&identities==0,"a skipped manifest still creates the service-owned dashboard");
    installed=true;identityCode=1;
    r=createRoomDashboard(&applications,&overlay,"fixture.vrmanifest",10,panel,thumbnail);
    check(r.installed&&r.identityError==1&&dashboards==2,"identity failure cannot hide the dashboard");
    manifestCode=1;
    r=createRoomDashboard(&applications,&overlay,"fixture.vrmanifest",10,panel,thumbnail);
    check(!r.manifestAdded&&dashboards==3,"manifest API errors leave the independent dashboard available");
    r=createRoomDashboard(nullptr,&overlay,"fixture.vrmanifest",10,panel,thumbnail);
    check(!r.manifestAdded&&dashboards==4,"missing application metadata API still permits an overlay");
    manifestCode=identityCode=0;
    r=createRoomDashboard(&applications,&overlay,"fixture.vrmanifest",10,panel,thumbnail);
    check(r.installed&&r.identityError==0&&adds==4&&dashboards==5,"accepted metadata is associated normally");
    std::puts("PASS dashboard startup survives rejected manifests and failed optional identification");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
