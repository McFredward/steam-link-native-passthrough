// Native dashboard and capture controller with an owned OpenXR room scene.
#include <openvr_capi.h>
#include "native_pipe.h"
#include "native_view.h"
#include "native_present.h"
#include "native_registration.h"
#include "native_dashboard.h"
#include "native_gpu.h"
#include "native_camera_source.h"
#include "native_projection.h"
#include "native_scan_view.h"
#include "native_scene_driver.h"
#include "native_hud_input.h"
#include "native_controller_aim.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <filesystem>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>

namespace {
using namespace room_native;
volatile sig_atomic_t stopping=0;
void signalStop(int){stopping=1;}
double seconds(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return double(t.tv_sec)+double(t.tv_nsec)*1e-9;}
std::string randomId(){std::array<unsigned char,16> bytes{};if(getrandom(bytes.data(),bytes.size(),0)!=ssize_t(bytes.size()))throw std::runtime_error("Random session unavailable");
    bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;char text[37]{};
    std::snprintf(text,sizeof(text),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);return text;
}
struct CaptureCommand {std::uint64_t epoch,attempt;unsigned kind;};
class Runtime {
public:
    VR_IVRSystem_FnTable* system=nullptr;VR_IVROverlay_FnTable* overlay=nullptr;
    VR_IVRApplications_FnTable* applications=nullptr;VR_IVRTrackedCamera_FnTable* camera=nullptr;
    VR_IVRRenderModels_FnTable* renderModels=nullptr;VR_IVRInput_FnTable* input=nullptr;
    VR_IVRCompositor_FnTable* compositor=nullptr;OverlayGpu gpu;
    bool graphicsAvailable=false,sceneAvailable=false,hudInteractive=false;std::array<std::vector<std::uint8_t>,2> fallbackBytes;
    VROverlayHandle_t panel=0,thumbnail=0,hud=0;
    std::string manifest;
    std::array<TextureOverlayImage,3> images{{TextureOverlayImage{},TextureOverlayImage{},TextureOverlayImage{.05}}};
    std::array<std::shared_ptr<const Json>,3> visibleUi;
    bool initialize(const std::string& library){
        char canonical[4096]{};if(library.empty()||library.front()!='/'||!realpath(library.c_str(),canonical)||library!=canonical
            ||std::filesystem::path(library).filename()!="libopenvr_api.so")return false;
        fd_=open(library.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat info{};Elf64_Ehdr elf{};
        if(fd_<0||fstat(fd_,&info)||!S_ISREG(info.st_mode)||(info.st_mode&0022)||(info.st_uid!=0&&info.st_uid!=getuid())
            ||info.st_size<64||info.st_size>8*1024*1024||pread(fd_,&elf,sizeof(elf),0)!=ssize_t(sizeof(elf))
            ||std::memcmp(elf.e_ident,ELFMAG,SELFMAG)||elf.e_machine!=EM_AARCH64)return false;
        module_=dlopen(library.c_str(),RTLD_NOW|RTLD_LOCAL);if(!module_)return false;
        struct stat after{};if(stat(library.c_str(),&after)||info.st_dev!=after.st_dev||info.st_ino!=after.st_ino)return false;
        if(!symbol(init_,"VR_InitInternal2")||!symbol(get_,"VR_GetGenericInterface")||!symbol(shutdown_,"VR_ShutdownInternal"))return false;
        EVRInitError error=EVRInitError_VRInitError_None;init_(&error,EVRApplicationType_VRApplication_Overlay,nullptr);if(error)return false;started_=true;
        if(!table(system,"FnTable:IVRSystem_026")||!table(overlay,"FnTable:IVROverlay_028"))return false;
        table(compositor,"FnTable:IVRCompositor_029");
        table(applications,"FnTable:IVRApplications_008");
        table(camera,"FnTable:IVRTrackedCamera_006");
        table(renderModels,"FnTable:IVRRenderModels_006");table(input,"FnTable:IVRInput_011");
        char model[128]{};ETrackedPropertyError e=ETrackedPropertyError_TrackedProp_Success;
        const auto size=system->GetStringTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_ModelNumber_String,model,sizeof(model),&e);
        if(e||!size||size>sizeof(model)||model[size-1]||(std::strcmp(model,"Steam Frame")&&std::strcmp(model,"Deckard MP")))return false;
        return true;
    }
    bool create(const std::string& root){
        manifest=root+"/room.vrmanifest";
        const auto registration=createRoomDashboard(applications,overlay,manifest,std::uint32_t(getpid()),panel,thumbnail);
        registered_=registration.manifestAdded;
        std::fprintf(stderr,"Room application: key=%s manifest_code=%d installed=%d identity_code=%d dashboard_code=%d\n",
            RoomApplicationKey,registration.manifestError,int(registration.installed),registration.identityError,int(registration.overlayError));
        if(registration.overlayError)throw std::runtime_error("Room dashboard creation failed code="+std::to_string(int(registration.overlayError)));
        try{gpu.initialize(system,compositor);graphicsAvailable=true;
            std::fprintf(stderr,"Room renderer: persistent Vulkan textures initialized\n");}
        catch(const std::exception& error){std::fprintf(stderr,"Room renderer: graphics unavailable: %s\n",error.what());}
        sceneAvailable=trustedSceneExecutable(root+"/sfnp-room-renderer");
        std::fprintf(stderr,"Room scene: owned native renderer available=%d\n",int(sceneAvailable));
        HmdVector2_t scale{{1024,800}};
        if(overlay->SetOverlayWidthInMeters(panel,1.8f)||overlay->SetOverlayMouseScale(panel,&scale)
            ||overlay->SetOverlayInputMethod(panel,VROverlayInputMethod_Mouse))return false;
        Canvas iconImage(128,128);icon(iconImage);
        publish(1,iconImage,seconds());
        std::string hudKey=std::string(RoomApplicationKey)+".controls";char hudName[]="Room Scan Controls";
        if(overlay->CreateOverlay(hudKey.data(),hudName,&hud))return false;
        overlay->SetOverlayFlag(panel,VROverlayFlags_IsPremultiplied,true);
        overlay->SetOverlayFlag(hud,VROverlayFlags_IsPremultiplied,true);
        overlay->SetOverlayFlag(hud,VROverlayFlags_NoBackside,true);
        overlay->SetOverlaySortOrder(hud,UINT32_MAX-1);
        hudInteractive=RoomHudInput::configure(overlay,hud);
        std::fprintf(stderr,"Room input: native HUD pointer and mouse available=%d\n",int(hudInteractive));
        overlay->SetOverlayWidthInMeters(hud,MiniPanelWidth);const auto panelPose=miniPanelPose();HmdMatrix34_t relative{};std::memcpy(&relative,&panelPose,sizeof(Mat));
        if(overlay->SetOverlayTransformTrackedDeviceRelative(hud,0,&relative))return false;
        return true;
    }
    VROverlayHandle_t imageHandle(unsigned index)const{
        if(index>2)throw std::runtime_error("Unknown room UI image");
        return index==0?panel:index==1?thumbnail:hud;
    }
    const char* imageName(unsigned index)const{
        static const char* names[]{"dashboard","thumbnail","controls"};return names[index];
    }
    bool publish(unsigned index,const Canvas& image,double now){
        return images.at(index).update(image,now,imageName(index),[&](const Canvas& canvas){
            if(!graphicsAvailable){
                // A single immutable diagnostic and icon remain visible if GPU
                // startup fails. No hover, animation or camera actions run here.
                if(index>1||images[index].loaded())throw std::runtime_error("Static graphics diagnostic cannot change");
                fallbackBytes[index]=canvas.pixels;
                return int(overlay->SetOverlayRaw(imageHandle(index),fallbackBytes[index].data(),unsigned(canvas.width),unsigned(canvas.height),4));
            }
            return gpu.present(index,canvas,[&](Texture_t* texture){return overlay->SetOverlayTexture(imageHandle(index),texture);});});
    }
    void publishUi(unsigned index,const Canvas& image,std::shared_ptr<const Json> state,double now){
        if(publish(index,image,now)||images[index].matches(image))visibleUi.at(index)=std::move(state);
    }
    ~Runtime(){
        if(overlay){for(const auto handle:{hud})if(handle){overlay->HideOverlay(handle);overlay->ClearOverlayTexture(handle);overlay->DestroyOverlay(handle);}
            for(const auto handle:{panel,thumbnail})if(handle){overlay->ClearOverlayTexture(handle);overlay->DestroyOverlay(handle);}}
        if(registered_&&applications&&applications->RemoveApplicationManifest)applications->RemoveApplicationManifest(manifest.data());
        if(started_)shutdown_();
        if(module_)dlclose(module_);
        if(fd_>=0)close(fd_);
    }
private:
    template<class T>bool symbol(T& value,const char* name){void* p=dlsym(module_,name);std::memcpy(&value,&p,sizeof(p));return value!=nullptr;}
    template<class T>bool table(T*& value,const char* name){EVRInitError error=EVRInitError_VRInitError_None;value=static_cast<T*>(get_(name,&error));if(error)value=nullptr;return value!=nullptr;}
    int fd_=-1;void* module_=nullptr;bool started_=false,registered_=false;
    std::uint32_t(*init_)(EVRInitError*,EVRApplicationType,const char*)=nullptr;
    void*(*get_)(const char*,EVRInitError*)=nullptr;void(*shutdown_)()=nullptr;
};
void miniPanel(Canvas& image,const Json& state,const std::string& hovered){image.clear();image.rect(0,0,1024,240,{12,24,30,235},22);
    image.text(24,14,state.value("title","Room Scan"),30,Ink,880);
    const auto notice=state.value("notice",std::string{});
    image.text(24,85,notice.empty()?state.value("subtitle",std::string{}):notice,19,notice.empty()?Muted:Amber,970);
    closeButton(image,hovered);
    for(std::size_t i=0;i<std::min<std::size_t>(3,state["buttons"].size());++i){const auto& b=state["buttons"][i];const bool enabled=b.value("enabled",false);
        image.rect(20+int(i)*334,145,314,70,enabled?(hovered==b.value("action","")?Color{43,112,112}:Color{29,79,82}):Color{25,40,45},12);
        image.text(35+int(i)*334,162,b.value("label",""),21,enabled?Ink:Muted,285,1);}}
}
int main(int argc,char** argv){
    using namespace room_native;
    try{
        std::string library,data,preview,previewWorld,output;
        for(int i=1;i<argc;++i){const std::string flag=argv[i];if(i+1>=argc)throw std::runtime_error("Missing argument");
            const std::string value=argv[++i];if(flag=="--openvr")library=value;else if(flag=="--data")data=value;
            else if(flag=="--preview")preview=value;else if(flag=="--preview-world")previewWorld=value;
            else if(flag=="--output")output=value;else throw std::runtime_error("Unknown argument");}
        if(!preview.empty()||!previewWorld.empty()){
            if(output.empty())throw std::runtime_error("Preview output required");
            Canvas image(previewWorld.empty()?1024:512,previewWorld.empty()?800:512);
            if(previewWorld.empty())dashboard(image,boundedJson(preview,16*1024*1024),1);
            else {const auto state=boundedJson(previewWorld,16*1024*1024);MeshView mesh;mesh.update(state,0);
                mesh.render(image,Mat{},Frustum{},1);}
            std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(image.pixels.data()),std::streamsize(image.pixels.size()));return file?0:1;
        }
        if(library.empty()||data.empty())throw std::runtime_error("Explicit installed OpenVR library and room-data directory required");
        char executable[4096]{};const auto count=readlink("/proc/self/exe",executable,sizeof(executable)-1);if(count<=0)throw std::runtime_error("Executable location unavailable");
        const auto root=std::filesystem::path(executable).parent_path().string();
        signal(SIGTERM,signalStop);signal(SIGINT,signalStop);signal(SIGHUP,signalStop);signal(SIGPIPE,SIG_IGN);
        Runtime runtime;if(!runtime.initialize(library)||!runtime.create(root))throw std::runtime_error("Native dashboard setup unavailable");
        Backend backend;if(!backend.start(root,data))throw std::runtime_error("Room controller unavailable");
        RoomHudInput hudInput;
        DashboardControl dashboardControl(runtime.overlay,runtime.panel);
        RoomSceneDriver scene(runtime.system,root+"/sfnp-room-renderer",(std::filesystem::path(library).parent_path()/"libopenxr_loader.so").string());
        ScanDashboardHider immersiveView(library);ScanViewTransition viewTransition;std::string restoreDashboard;
        auto origin=std::make_shared<const std::string>(randomId());
        std::shared_ptr<const CaptureCommand> beginCapture,pendingCapture;
        std::atomic<std::uint64_t> captureEpoch{0},captureAttempt{0},cameraFailedAttempt{0};
        std::atomic<bool> rendererFailed{false};
        std::atomic<bool> endWorker{false},invalidate{false},roomClosed{false};
        std::thread cameraWorker([&]{RoomStereoCamera camera;camera.system=runtime.system;camera.api=runtime.camera;
            camera.visionLibrary=root+"/libsfnp-room-vision.so";
            camera.pcMode=true;
            bool awaiting=false,collecting=false;double started=0,lastFrame=0,lastHint=0,lastAccepted=0;std::string previousHint;std::uint64_t activeEpoch=0,activeAttempt=0;
            while(!endWorker&&!stopping&&backend.alive){
                if(activeEpoch!=captureEpoch.load()||activeAttempt!=captureAttempt.load()){camera.stop();awaiting=collecting=false;activeEpoch=captureEpoch.load();activeAttempt=captureAttempt.load();}
                if(roomClosed){camera.stop();awaiting=false;std::this_thread::sleep_for(std::chrono::milliseconds(30));continue;}
                if(invalidate.exchange(false)){camera.stop();backend.send({{"event","tracking_lost"}});awaiting=false;}
                if(const auto request=std::atomic_exchange(&beginCapture,std::shared_ptr<const CaptureCommand>{})){
                    const auto epoch=request->epoch,attempt=request->attempt;const auto kind=request->kind;
                    if(roomClosed||epoch!=captureEpoch.load()||attempt!=captureAttempt.load()||rendererFailed)continue;
                    activeAttempt=attempt;
                    TrackedDevicePose_t pose{};runtime.system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&pose,1);
                    const bool validTracking=tracked(pose);
                    if(validTracking&&camera.start(randomId(),*std::atomic_load(&origin))){
                        std::fprintf(stderr,"Room camera: %s\n",camera.diagnostics.dump().c_str());
                        if(roomClosed||epoch!=captureEpoch.load()||attempt!=captureAttempt.load()||rendererFailed){camera.stop();continue;}
                        awaiting=true;started=lastFrame=lastAccepted=seconds();backend.send({{"event",kind==2?"camera_resumed":"camera_ready"},{"capture_epoch",epoch},{"binding",camera.binding},{"tracking_valid",true}});}
                    else {const std::string reason=!validTracking?"head-tracking-unavailable":camera.status;
                        const Json diagnostic=validTracking?camera.diagnostics:Json{{"check","head-tracking"},{"reason",reason}};
                        std::fprintf(stderr,"Room camera: %s\n",diagnostic.dump().c_str());backend.send({{"event","camera_unavailable"},{"capture_epoch",epoch},{"reason",reason}});
                        if(attempt==captureAttempt.load())cameraFailedAttempt=attempt;}
                }
                const auto state=backend.latest();
                const bool wantsFrames=state&&state->value("capture_collecting",false)&&!rendererFailed;
                if(wantsFrames&&!collecting)lastFrame=seconds();
                collecting=wantsFrames;
                if(state&&camera.active()){
                    if(state->value("capture_active",false))awaiting=false;
                    if((!awaiting&&!state->value("capture_active",false))||(awaiting&&seconds()-started>5))camera.stop();
                    else if(wantsFrames){
                        try{auto frame=camera.next();if(!frame.empty()){
                                if(roomClosed||activeEpoch!=captureEpoch.load()||activeAttempt!=captureAttempt.load()||rendererFailed){camera.stop();continue;}
                                frame["capture_epoch"]=activeEpoch;lastFrame=seconds();
                                if(frame.value("event","")=="capture_hint"){
                                    const auto hint=frame.value("message","");const auto interval=hint==previousHint?1.:.5;
                                    if(seconds()-lastAccepted>2&&seconds()-lastHint>=interval){backend.send(frame,true);lastHint=seconds();previousHint=hint;}
                                }else {backend.send(frame,true);lastAccepted=seconds();previousHint.clear();}}
                            else if(seconds()-lastFrame>5)throw std::runtime_error("Camera observation timed out");}
                        catch(const std::exception& e){std::fprintf(stderr,"Room camera: %s\n",e.what());camera.stop();backend.send({{"event","tracking_lost"}});}
                    }
                    else if(!awaiting){try{camera.park();}catch(const std::exception&){camera.stop();backend.send({{"event","tracking_lost"}});}}
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(camera.active()&&wantsFrames?5:30));
            }
            camera.stop();
        });
        struct Join {std::thread& worker;std::atomic<bool>& stop;~Join(){stop=true;if(worker.joinable())worker.join();}} join{cameraWorker,endWorker};
        Canvas panel(1024,800),hud(1024,240);MeshView mesh;
        std::array<bool,64> triggers{},seenControllers{};std::string hovered,keyboardEvent="rename";bool spatial=false,originInvalid=false;double lastPanel=0;
        std::array<Mat,64> controllerPoses{},controllerAims{};std::array<bool,64> controllerValid{};
        double lastRenderReport=0;unsigned renderReports=0,reopenReports=0,processReports=0;bool closePending=false,closeHidden=false;
        bool frameSynced=false;
        scene.unavailable=[&]{rendererFailed=true;++captureAttempt;pendingCapture.reset();std::atomic_store(&beginCapture,std::shared_ptr<const CaptureCommand>{});
            immersiveView.cancel();viewTransition.reset();
            backend.send({{"event","renderer_unavailable"},{"capture_epoch",captureEpoch.load()}});
            if(!roomClosed){restoreDashboard=RoomApplicationKey;runtime.overlay->ShowDashboard(restoreDashboard.data());}};
        scene.originChanged=[&]{std::atomic_store(&origin,std::make_shared<const std::string>(randomId()));invalidate=true;originInvalid=true;spatial=false;};
        scene.exited=[&]{if(!restoreDashboard.empty())runtime.overlay->ShowDashboard(restoreDashboard.data());};
        scene.hint=[&](const std::string& message){backend.send({{"event","capture_hint"},{"capture_epoch",captureEpoch.load()},{"message",message}},true);};
        const auto action=[&](const std::string& text){
            if(text=="close"){
                if(roomClosed.exchange(true))return;
                restoreDashboard=(scene.running()||runtime.overlay->IsDashboardVisible())?"valve.steam.gamepadui.main":"";
                scene.requestStop();pendingCapture.reset();++captureAttempt;
                immersiveView.cancel();viewTransition.reset();
                closeHidden=false;
                std::atomic_store(&beginCapture,std::shared_ptr<const CaptureCommand>{});const auto epoch=captureEpoch.fetch_add(1)+1;
                closePending=!backend.send({{"event","close"},{"capture_epoch",epoch}});
                runtime.overlay->HideKeyboard();
                runtime.overlay->HideOverlay(runtime.panel);runtime.overlay->HideOverlay(runtime.hud);
                if(!restoreDashboard.empty())runtime.overlay->ShowDashboard(restoreDashboard.data());
                if(dashboardControl.requestClose())std::fprintf(stderr,"Room dashboard: returned to SteamVR menu\n");
                else std::fprintf(stderr,"Room dashboard: SteamVR menu unavailable\n");
                spatial=false;hovered.clear();controllerValid.fill(false);triggers.fill(false);seenControllers.fill(false);
                hudInput.reset();
                std::fprintf(stderr,"Room window closed; capture cancelled and dashboard entry retained\n");
                return;
            }
            if(roomClosed)return;
            if(text.empty()||!runtime.graphicsAvailable)return;
            const auto state=backend.latest();if(!state)return;
            if(closePending||state->value("capture_epoch",std::uint64_t(0))!=captureEpoch.load())return;
            bool enabled=false;for(const auto& button:state->at("buttons"))
                if(button.value("action","")==text&&button.value("enabled",false))enabled=true;
            if(!enabled)return;
            if(text=="start"||text=="resume"||text=="align_start"){
                if(state->value("camera_check_pending",false)||std::atomic_load(&beginCapture)||pendingCapture)return;
                if((text=="start"&&state->value("page","")=="prepare")||text=="resume"||text=="align_start"){
                    const auto epoch=captureEpoch.load();
                    if(!state->value("pc_ready",false)){backend.send({{"event","camera_unavailable"},{"capture_epoch",epoch},{"reason","room-pc-unavailable"}});return;}
                    if(!runtime.hudInteractive){backend.send({{"event","camera_unavailable"},{"capture_epoch",epoch},{"reason","room-input-unavailable"}});return;}
                    if(!runtime.sceneAvailable){backend.send({{"event","camera_unavailable"},{"capture_epoch",epoch},{"reason","room-renderer-unavailable"}});return;}
                    const auto activeScene=runtime.applications&&runtime.applications->GetCurrentSceneProcessId?runtime.applications->GetCurrentSceneProcessId():0;
                    if(activeScene&&!scene.ownsPid(activeScene)){backend.send({{"event","camera_unavailable"},{"capture_epoch",epoch},{"reason","room-renderer-scene-busy"}});return;}
                    rendererFailed=false;const auto attempt=++captureAttempt;
                    pendingCapture=std::make_shared<const CaptureCommand>(CaptureCommand{epoch,attempt,text=="resume"?2u:1u});
                    backend.send({{"event","camera_warming"},{"capture_epoch",epoch}});scene.requestStart();}
                return;}
            if(text=="rename"||text=="rename_element"){
                keyboardEvent=text;
                std::string description=text=="rename"?"Name room":"Name element",existing;
                runtime.overlay->ShowKeyboardForOverlay(runtime.panel,EGamepadTextInputMode_k_EGamepadTextInputModeNormal,
                    EGamepadTextInputLineMode_k_EGamepadTextInputLineModeSingleLine,0,description.data(),80,existing.data(),0);return;
            }
            backend.send({{"event","action"},{"action",text}});
            if(text=="add"||text=="elements"||text=="review"){
                immersiveView.cancel();restoreDashboard=RoomApplicationKey;runtime.overlay->ShowDashboard(restoreDashboard.data());}
        };
        const auto graphicsUnavailable=std::make_shared<const Json>(Json{{"page","graphics_unavailable"},{"title","Room Setup"},
            {"subtitle","Room Setup could not initialize its graphics. Check for an update before trying again."},
            {"notice","Diagnostic code: room-graphics-unavailable"},{"buttons",Json::array()},{"room",nullptr},
            {"pending",nullptr},{"corners",Json::array()},{"world_revision",0}});
        while(!stopping&&backend.alive){
            scene.tick(*std::atomic_load(&origin),backend.latest(),seconds());
            if(pendingCapture&&scene.ready()){std::atomic_store(&beginCapture,std::move(pendingCapture));pendingCapture.reset();}
            if(const auto failedAttempt=cameraFailedAttempt.exchange(0);failedAttempt&&failedAttempt==captureAttempt.load()){
                scene.requestStop();immersiveView.cancel();restoreDashboard=RoomApplicationKey;runtime.overlay->ShowDashboard(restoreDashboard.data());}
            if(immersiveView.cancelledCompletion()&&!restoreDashboard.empty())runtime.overlay->ShowDashboard(restoreDashboard.data());
            if(roomClosed&&!closeHidden&&!runtime.overlay->IsActiveDashboardOverlay(runtime.panel)){
                closeHidden=true;
                std::fprintf(stderr,"Room dashboard: room panel inactive, SteamVR dashboard visible=%d\n",int(runtime.overlay->IsDashboardVisible()));
            }
            if(closePending&&backend.send({{"event","close"},{"capture_epoch",captureEpoch.load()}}))closePending=false;
            const double now=seconds();const auto state=runtime.graphicsAvailable?backend.latest():graphicsUnavailable;VREvent_t event{};
            if(state&&!roomClosed&&!rendererFailed&&viewTransition.observe(*state)){restoreDashboard.clear();immersiveView.request();}
            for(unsigned n=0;n<64&&runtime.system->PollNextEvent(&event,sizeof(event));++n){
                if(roomQuitRequested(event))stopping=1;
                if(event.eventType==EVREventType_VREvent_ProcessQuit&&processReports++<16)
                    std::fprintf(stderr,"Room application: client exit ignored pid=%u\n",event.data.process.pid);
                if(event.eventType==EVREventType_VREvent_ChaperoneUniverseHasChanged||event.eventType==EVREventType_VREvent_SeatedZeroPoseReset){
                    std::atomic_store(&origin,std::make_shared<const std::string>(randomId()));invalidate=true;originInvalid=true;spatial=false;}
            }
            for(unsigned n=0;n<64&&runtime.overlay->PollNextOverlayEvent(runtime.panel,&event,sizeof(event));++n){
                if(event.eventType==EVREventType_VREvent_Quit)stopping=1;
                if(event.eventType==EVREventType_VREvent_OverlayShown&&(!roomClosed||closeHidden)
                    &&runtime.overlay->IsActiveDashboardOverlay(runtime.panel)){
                    const bool reopened=roomClosed.exchange(false);closeHidden=false;lastPanel=0;
                    if(reopened){restoreDashboard=RoomApplicationKey;if(reopenReports++<32)std::fprintf(stderr,"Room dashboard: reopened after close\n");}}
                if(event.eventType==EVREventType_VREvent_MouseButtonUp&&event.data.mouse.button==EVRMouseButton_VRMouseButton_Left
                    &&hitClose(event.data.mouse.x,800-event.data.mouse.y)){action("close");continue;}
                if(roomClosed)continue;
                if(!state)continue;
                const auto displayed=runtime.visibleUi[0];
                if(!displayed||!sameControls(*displayed,*state))continue;
                if(event.eventType==EVREventType_VREvent_MouseMove)hovered=hitButton(*displayed,event.data.mouse.x,800-event.data.mouse.y);
                if(event.eventType==EVREventType_VREvent_MouseButtonUp&&event.data.mouse.button==EVRMouseButton_VRMouseButton_Left)
                    action(hitButton(*displayed,event.data.mouse.x,800-event.data.mouse.y));
                if(event.eventType==EVREventType_VREvent_KeyboardDone){char name[321]{};runtime.overlay->GetKeyboardText(name,sizeof(name));backend.send({{"event",keyboardEvent},{"name",name}});}
            }
            for(unsigned index=1;index<runtime.images.size();++index)
                for(unsigned n=0;n<64&&runtime.overlay->PollNextOverlayEvent(runtime.imageHandle(index),&event,sizeof(event));++n){
                    if(event.eventType==EVREventType_VREvent_Quit)stopping=1;
                    if(index==2){
                        const bool interactive=state&&!roomClosed&&!closePending&&scene.ready()
                            &&runtime.overlay->IsOverlayVisible(runtime.hud)&&!runtime.overlay->IsDashboardVisible();
                        if(interactive){const auto requested=hudInput.event(event,runtime.visibleUi[2],*state,true);
                            if(!requested.empty())action(requested);}
                        else hudInput.reset();}
                }
            if(stopping)break;
            if(roomClosed||closePending||(state&&state->value("capture_epoch",captureEpoch.load())!=captureEpoch.load())){
                hudInput.reset();
                scene.view(false,Json::array());std::this_thread::sleep_for(std::chrono::milliseconds(50));continue;}
            if(now-lastRenderReport>=10&&renderReports<120){
                std::fprintf(stderr,"Room renderer: panel_submissions=%llu panel_rate_skips=%llu native_scene=%d\n",
                    static_cast<unsigned long long>(runtime.images[0].submissions()),
                    static_cast<unsigned long long>(runtime.images[0].coalesced()),
                    int(scene.running()));
                lastRenderReport=now;++renderReports;
            }
            if(state){
                if(runtime.overlay->IsOverlayVisible(runtime.panel)&&now-lastPanel>.1){dashboard(panel,*state,std::floor(now*4)/4,hovered);
                    runtime.publishUi(0,panel,state,now);
                    lastPanel=now;}
                mesh.update(*state,now);TrackedDevicePose_t head{};
                if(!mesh.allowed&&!state->value("capture_active",false)){
                        runtime.overlay->HideOverlay(runtime.hud);
                    hudInput.reset();
                    controllerValid.fill(false);triggers.fill(false);seenControllers.fill(false);scene.view(false,Json::array());
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;
                }
                frameSynced=scene.running()&&runtime.overlay->WaitFrameSync&&!runtime.overlay->WaitFrameSync(100);
                const auto display=roomDisplayPrediction(runtime.system);
                runtime.system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,display.seconds,&head,1);
                if(!state->at("room").is_null()&&state->at("room").value("origin_id",std::string{})==*std::atomic_load(&origin))originInvalid=false;
                spatial=scene.ready()&&mesh.allowed&&tracked(head)&&!invalidate&&!originInvalid&&!rendererFailed;
                if(!tracked(head)&&mesh.allowed)invalidate=true;
                if(spatial){
                    if(!runtime.overlay->IsDashboardVisible()){miniPanel(hud,*state,hudInput.hovered());runtime.publishUi(2,hud,state,now);if(runtime.images[2].loaded())runtime.overlay->ShowOverlay(runtime.hud);
                        const bool placing=state->value("page","")=="place"||state->value("page","")=="align_place";
                        controllerValid.fill(false);
                        for(unsigned hand=0;hand<2;++hand){const unsigned device=hand+1;
                            const auto& controller=scene.controllers[hand];
                            if(!placing||!controller.valid||now-controller.updated>.15){seenControllers[device]=false;triggers[device]=false;continue;}
                            const auto& aim=controller.aim;const auto& controllerPose=controller.grip;const bool pressed=controller.pressed;
                            controllerValid[device]=true;controllerPoses[device]=controllerPose;controllerAims[device]=aim;
                            if(seenControllers[device]&&pressed&&!triggers[device]&&!hudBlocksRay(runtime.overlay,runtime.hud,aim)){
                                const auto displayed=runtime.visibleUi[2];
                                if(displayed&&sameControls(*displayed,*state)){
                                    const V3 p=position(controllerPose),source=position(aim),d=column(aim,2)*(-1);
                                    backend.send({{"event","point"},{"origin_id",*std::atomic_load(&origin)},{"sample_ns",std::uint64_t(now*1e9)},
                                        {"controller_position",{p.x,p.y,p.z}},{"ray_origin",{source.x,source.y,source.z}},{"ray_direction",{d.x,d.y,d.z}}});
                                }
                            }
                            triggers[device]=pressed;seenControllers[device]=true;
                        }
                    }else {runtime.overlay->HideOverlay(runtime.hud);hudInput.reset();controllerValid.fill(false);triggers.fill(false);seenControllers.fill(false);}
                }else {runtime.overlay->HideOverlay(runtime.hud);hudInput.reset();}
                Json pointers=Json::array();
                if(spatial&&!runtime.overlay->IsDashboardVisible())for(unsigned device=1;device<64&&pointers.size()<2;++device)if(controllerValid[device]){
                    // SteamVR draws the menu pointer. Never draw a second ray
                    // through its actual interactive overlay surface.
                    if(hudBlocksRay(runtime.overlay,runtime.hud,controllerAims[device]))continue;
                    const auto source=position(controllerAims[device]),direction=column(controllerAims[device],2)*-1;
                    const auto hit=mesh.rayHit(source,direction);const bool marker=state->value("controller_marker",false);
                    const auto target=marker?position(controllerPoses[device]):hit?*hit:source+direction*2;
                    pointers.push_back({{"origin",{source.x,source.y,source.z}},{"target",{target.x,target.y,target.z}},{"marker",marker||bool(hit)}});
                }
                scene.view(spatial,pointers);
            }
            if(!spatial||!frameSynced)std::this_thread::sleep_for(std::chrono::milliseconds(spatial?1:50));
        }
        if(!stopping)throw std::runtime_error("Room controller stopped unexpectedly");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Room setup: %s\n",e.what());return 1;}
}
