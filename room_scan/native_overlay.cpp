// Native local dashboard + two renderer-owned per-eye projection overlays.
#include <openvr_capi.h>
#include "native_pipe.h"
#include "native_view.h"
#include "native_present.h"
#include "native_registration.h"
#include "native_gpu.h"
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
class Runtime {
public:
    VR_IVRSystem_FnTable* system=nullptr;VR_IVROverlay_FnTable* overlay=nullptr;
    VR_IVRApplications_FnTable* applications=nullptr;VR_IVRTrackedCamera_FnTable* camera=nullptr;
    VR_IVRCompositor_FnTable* compositor=nullptr;OverlayGpu gpu;
    bool graphicsAvailable=false;std::array<std::vector<std::uint8_t>,2> fallbackBytes;
    VROverlayHandle_t panel=0,thumbnail=0,hud=0;std::array<VROverlayHandle_t,2> world{};
    std::string manifest;
    std::array<TextureOverlayImage,5> images{{TextureOverlayImage{},TextureOverlayImage{},TextureOverlayImage{},TextureOverlayImage{.03},TextureOverlayImage{.03}}};
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
        char model[128]{};ETrackedPropertyError e=ETrackedPropertyError_TrackedProp_Success;
        const auto size=system->GetStringTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_ModelNumber_String,model,sizeof(model),&e);
        if(e||!size||size>sizeof(model)||model[size-1]||(std::strcmp(model,"Steam Frame")&&std::strcmp(model,"Deckard MP")))return false;
        return true;
    }
    bool create(const std::string& root){
        manifest=root+"/room.vrmanifest";
        const auto registration=createRoomDashboard(applications,overlay,manifest,std::uint32_t(getpid()),panel,thumbnail);
        registered_=registration.manifestAdded;
        std::fprintf(stderr,"Room application: key=sfnp.room.setup manifest_code=%d installed=%d identity_code=%d dashboard_code=%d\n",
            registration.manifestError,int(registration.installed),registration.identityError,int(registration.overlayError));
        if(registration.overlayError)throw std::runtime_error("Room dashboard creation failed code="+std::to_string(int(registration.overlayError)));
        try{gpu.initialize(system,compositor);graphicsAvailable=true;
            std::fprintf(stderr,"Room renderer: persistent Vulkan textures initialized\n");}
        catch(const std::exception& error){std::fprintf(stderr,"Room renderer: graphics unavailable: %s\n",error.what());}
        HmdVector2_t scale{{1024,800}};
        if(overlay->SetOverlayWidthInMeters(panel,1.8f)||overlay->SetOverlayMouseScale(panel,&scale)
            ||overlay->SetOverlayInputMethod(panel,VROverlayInputMethod_Mouse))return false;
        Canvas iconImage(128,128);icon(iconImage);
        publish(1,iconImage,seconds());
        for(unsigned eye=0;eye<2;++eye){const auto id="sfnp.room.mesh."+std::to_string(eye);std::string label="Room Scan";
            if(overlay->CreateOverlay(const_cast<char*>(id.c_str()),label.data(),&world[eye]))return false;
            overlay->SetOverlayInputMethod(world[eye],VROverlayInputMethod_None);
            overlay->SetOverlayFlag(world[eye],VROverlayFlags_IsPremultiplied,true);
            overlay->SetOverlayFlag(world[eye],VROverlayFlags_NoBackside,true);
            overlay->SetOverlayFlag(world[eye],VROverlayFlags_VisibleInDashboard,true);
            overlay->SetOverlaySortOrder(world[eye],UINT32_MAX-2);
        }
        char hudKey[]="sfnp.room.controls",hudName[]="Room Scan Controls";
        if(overlay->CreateOverlay(hudKey,hudName,&hud))return false;
        overlay->SetOverlayFlag(panel,VROverlayFlags_IsPremultiplied,true);
        overlay->SetOverlayFlag(hud,VROverlayFlags_IsPremultiplied,true);
        overlay->SetOverlayFlag(hud,VROverlayFlags_NoBackside,true);
        overlay->SetOverlaySortOrder(hud,UINT32_MAX-1);
        overlay->SetOverlayInputMethod(hud,VROverlayInputMethod_None);
        overlay->SetOverlayWidthInMeters(hud,1.0f);HmdMatrix34_t relative{{{1,0,0,0},{0,1,0,-.4f},{0,0,1,-1.3f}}};
        if(overlay->SetOverlayTransformTrackedDeviceRelative(hud,0,&relative))return false;
        return true;
    }
    VROverlayHandle_t imageHandle(unsigned index)const{
        return index==0?panel:index==1?thumbnail:index==2?hud:world.at(index-3);
    }
    const char* imageName(unsigned index)const{
        static const char* names[]{"dashboard","thumbnail","controls","left-mesh","right-mesh"};return names[index];
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
    void publishWorld(unsigned eye,const Canvas& image,const Mat& pose,const Frustum& f,double now){
        const unsigned index=3+eye;
        if(publish(index,image,now)||images[index].matches(image)){
            HmdMatrix34_t nativePose{};std::memcpy(&nativePose,&pose,sizeof(Mat));VROverlayProjection_t frustum{f.left,f.right,f.top,f.bottom};
            const auto error=overlay->SetOverlayTransformProjection(world[eye],ETrackingUniverseOrigin_TrackingUniverseStanding,&nativePose,&frustum,EVREye(eye));
            if(error)throw std::runtime_error("Native stereo projection failed code="+std::to_string(int(error)));
        }
        if(images[index].loaded())overlay->ShowOverlay(world[eye]);
    }
    ~Runtime(){
        if(overlay){for(const auto handle:{world[0],world[1],hud})if(handle){overlay->HideOverlay(handle);overlay->ClearOverlayTexture(handle);overlay->DestroyOverlay(handle);}
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
std::string hudAction(const Json& state,const Mat& head,const Mat& controller){
    Mat relative;relative.m[1][3]=-.4f;relative.m[2][3]=-1.3f;const Mat h=multiply(head,relative),inv=inverse(h);
    const V3 source=transform(inv,position(controller)),direction=transform(inv,position(controller)+column(controller,2)*(-1))-source;
    if(std::abs(direction.z)<1e-6f)return {};
    const float t=-source.z/direction.z;if(t<=0)return {};
    const V3 point=source+direction*t;const float x=(point.x+.5f)*1024,y=(.5f-point.y/(240.f/1024))*240;
    if(y<145||y>215)return {};
    for(std::size_t i=0;i<std::min<std::size_t>(3,state["buttons"].size());++i){const auto& b=state["buttons"][i];
        if(b.value("enabled",false)&&x>=20+i*334&&x<=334+i*334)return b.value("action","");}return {};
}
void miniPanel(Canvas& image,const Json& state){image.clear();image.rect(0,0,1024,240,{12,24,30,235},22);
    image.text(24,14,state.value("title","Room Scan"),30,Ink);image.text(24,65,state.value("subtitle",""),19,Muted,970);
    for(std::size_t i=0;i<std::min<std::size_t>(3,state["buttons"].size());++i){const auto& b=state["buttons"][i];const bool enabled=b.value("enabled",false);
        image.rect(20+int(i)*334,145,314,70,enabled?Color{29,79,82}:Color{25,40,45},12);
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
        auto origin=std::make_shared<const std::string>(randomId());
        std::atomic<int> beginCapture{0};std::atomic<bool> endWorker{false},invalidate{false};
        std::thread cameraWorker([&]{StereoCamera camera;camera.system=runtime.system;camera.api=runtime.camera;
            bool awaiting=false;double started=0,lastFrame=0;
            while(!endWorker&&!stopping&&backend.alive){
                if(invalidate.exchange(false)){camera.stop();backend.send({{"event","tracking_lost"}});awaiting=false;}
                if(const int request=beginCapture.exchange(0)){
                    TrackedDevicePose_t pose{};runtime.system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&pose,1);
                    const bool validTracking=tracked(pose);
                    if(validTracking&&camera.start(randomId(),*std::atomic_load(&origin))){
                        std::fprintf(stderr,"Room camera: %s\n",camera.diagnostics.dump().c_str());
                        awaiting=true;started=lastFrame=seconds();backend.send({{"event",request==2?"camera_resumed":"camera_ready"},{"binding",camera.binding},{"tracking_valid",true}});}
                    else {const std::string reason=validTracking?camera.status:"head-tracking-unavailable";
                        const Json diagnostic=validTracking?camera.diagnostics:Json{{"check","head-tracking"},{"reason",reason}};
                        std::fprintf(stderr,"Room camera: %s\n",diagnostic.dump().c_str());backend.send({{"event","camera_unavailable"},{"reason",reason}});}
                }
                const auto state=backend.latest();
                if(state&&camera.handle){
                    if(state->value("capture_active",false))awaiting=false;
                    if((!awaiting&&!state->value("capture_active",false))||(awaiting&&seconds()-started>5))camera.stop();
                    else if(state->value("capture_collecting",false)){
                        try{auto frame=camera.next();if(!frame.empty()){lastFrame=seconds();backend.send(frame,true);}
                            else if(seconds()-lastFrame>5)throw std::runtime_error("Camera observation timed out");}
                        catch(const std::exception& e){std::fprintf(stderr,"Room camera: %s\n",e.what());camera.stop();backend.send({{"event","tracking_lost"}});}
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            camera.stop();
        });
        struct Join {std::thread& worker;std::atomic<bool>& stop;~Join(){stop=true;if(worker.joinable())worker.join();}} join{cameraWorker,endWorker};
        Canvas panel(1024,800),hud(1024,240);std::array<Canvas,2> worlds{Canvas(512,512),Canvas(512,512)};MeshView mesh;
        std::array<bool,64> triggers{},seenControllers{};std::string hovered,keyboardEvent="rename";bool spatial=false,originInvalid=false;double lastPanel=0;
        std::array<Mat,64> controllerPoses{};std::array<bool,64> controllerValid{};
        double lastRenderReport=0;unsigned renderReports=0;
        const auto action=[&](const std::string& text){
            if(text.empty()||!runtime.graphicsAvailable)return;
            const auto state=backend.latest();if(!state)return;
            bool enabled=false;for(const auto& button:state->at("buttons"))
                if(button.value("action","")==text&&button.value("enabled",false))enabled=true;
            if(!enabled)return;
            if(text=="start"||text=="resume"||text=="align_start"){
                if(state->value("camera_check_pending",false)||beginCapture.load()!=0)return;
                if((text=="start"&&state->value("page","")=="prepare")||text=="resume"||text=="align_start"){
                    backend.send({{"event","camera_warming"}});beginCapture=text=="resume"?2:1;}
                return;}
            if(text=="rename"||text=="rename_element"){
                keyboardEvent=text;
                std::string description=text=="rename"?"Name room":"Name element",existing;
                runtime.overlay->ShowKeyboardForOverlay(runtime.panel,EGamepadTextInputMode_k_EGamepadTextInputModeNormal,
                    EGamepadTextInputLineMode_k_EGamepadTextInputLineModeSingleLine,0,description.data(),80,existing.data(),0);return;
            }
            backend.send({{"event","action"},{"action",text}});
            if(text=="add"||text=="elements"){char key[]="sfnp.room.setup";runtime.overlay->ShowDashboard(key);}
        };
        const auto graphicsUnavailable=std::make_shared<const Json>(Json{{"page","graphics_unavailable"},{"title","Room Setup"},
            {"subtitle","Room Setup could not initialize its graphics. Check for an update before trying again."},
            {"notice","Diagnostic code: room-graphics-unavailable"},{"buttons",Json::array()},{"room",nullptr},
            {"pending",nullptr},{"corners",Json::array()},{"world_revision",0}});
        while(!stopping&&backend.alive){
            const double now=seconds();const auto state=runtime.graphicsAvailable?backend.latest():graphicsUnavailable;VREvent_t event{};
            for(unsigned n=0;n<64&&runtime.system->PollNextEvent(&event,sizeof(event));++n){
                if(event.eventType==EVREventType_VREvent_Quit||event.eventType==EVREventType_VREvent_ProcessQuit)stopping=1;
                if(event.eventType==EVREventType_VREvent_ChaperoneUniverseHasChanged||event.eventType==EVREventType_VREvent_SeatedZeroPoseReset){
                    std::atomic_store(&origin,std::make_shared<const std::string>(randomId()));invalidate=true;originInvalid=true;spatial=false;}
            }
            for(unsigned n=0;n<64&&runtime.overlay->PollNextOverlayEvent(runtime.panel,&event,sizeof(event));++n){
                if(event.eventType==EVREventType_VREvent_Quit)stopping=1;
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
                }
            if(stopping)break;
            if(now-lastRenderReport>=10&&renderReports<120){
                std::fprintf(stderr,"Room renderer: panel_submissions=%llu panel_rate_skips=%llu world_submissions=%llu\n",
                    static_cast<unsigned long long>(runtime.images[0].submissions()),
                    static_cast<unsigned long long>(runtime.images[0].coalesced()),
                    static_cast<unsigned long long>(runtime.images[3].submissions()+runtime.images[4].submissions()));
                lastRenderReport=now;++renderReports;
            }
            if(state){
                if(runtime.overlay->IsOverlayVisible(runtime.panel)&&now-lastPanel>.1){dashboard(panel,*state,std::floor(now*4)/4,hovered);
                    runtime.publishUi(0,panel,state,now);
                    lastPanel=now;}
                mesh.update(*state,now);TrackedDevicePose_t head{};
                if(!mesh.allowed&&!state->value("capture_active",false)){
                    for(auto handle:runtime.world)runtime.overlay->HideOverlay(handle);
                    runtime.overlay->HideOverlay(runtime.hud);
                    controllerValid.fill(false);triggers.fill(false);seenControllers.fill(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));continue;
                }
                runtime.system->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&head,1);
                const Mat headPose=fromNative(head.mDeviceToAbsoluteTracking);
                if(!state->at("room").is_null()&&state->at("room").value("origin_id",std::string{})==*std::atomic_load(&origin))originInvalid=false;
                spatial=mesh.allowed&&tracked(head)&&!invalidate&&!originInvalid;
                if(!tracked(head)&&mesh.allowed)invalidate=true;
                if(spatial){
                    for(unsigned eye=0;eye<2;++eye){const Mat eyeOffset=fromNative(runtime.system->GetEyeToHeadTransform(EVREye(eye)));
                        const Mat eyePose=multiply(headPose,eyeOffset);Frustum f;runtime.system->GetProjectionRaw(EVREye(eye),&f.left,&f.right,&f.top,&f.bottom);
                        mesh.render(worlds[eye],eyePose,f,now);
                        if(!runtime.overlay->IsDashboardVisible())for(unsigned device=1;device<64;++device)if(controllerValid[device]){
                            const V3 source=position(controllerPoses[device]),direction=column(controllerPoses[device],2)*(-1);
                            if(state->value("controller_marker",false))pointer(worlds[eye],eyePose,f,source,source,true);
                            else{const auto hit=mesh.rayHit(source,direction);const V3 target=hit?*hit:source+direction*2;
                                pointer(worlds[eye],eyePose,f,source,target,false);if(hit)pointer(worlds[eye],eyePose,f,source,target,true);}
                        }
                        runtime.publishWorld(eye,worlds[eye],eyePose,f,now);
                    }
                    if(!runtime.overlay->IsDashboardVisible()){miniPanel(hud,*state);runtime.publishUi(2,hud,state,now);if(runtime.images[2].loaded())runtime.overlay->ShowOverlay(runtime.hud);
                        for(unsigned device=1;device<64;++device){controllerValid[device]=false;
                            if(runtime.system->GetTrackedDeviceClass(device)!=ETrackedDeviceClass_TrackedDeviceClass_Controller)continue;
                            VRControllerState_t input{};TrackedDevicePose_t controller{};
                            if(!runtime.system->GetControllerStateWithPose(ETrackingUniverseOrigin_TrackingUniverseStanding,device,&input,sizeof(input),&controller)||!tracked(controller)){
                                seenControllers[device]=false;continue;}
                            const bool pressed=(input.ulButtonPressed&(1ULL<<33))!=0;const Mat controllerPose=fromNative(controller.mDeviceToAbsoluteTracking);
                            controllerValid[device]=rigid(controllerPose);controllerPoses[device]=controllerPose;
                            if(seenControllers[device]&&pressed&&!triggers[device]&&rigid(controllerPose)){
                                const auto displayed=runtime.visibleUi[2];
                                const bool visible=displayed&&sameControls(*displayed,*state);
                                const auto hit=visible?hudAction(*displayed,headPose,controllerPose):std::string{};
                                if(!hit.empty())action(hit);
                                else if(visible&&(state->value("page","")=="place"||state->value("page","")=="align_place")){
                                    const V3 p=position(controllerPose),d=column(controllerPose,2)*(-1);
                                    backend.send({{"event","point"},{"origin_id",*std::atomic_load(&origin)},{"sample_ns",std::uint64_t(now*1e9)},
                                        {"controller_position",{p.x,p.y,p.z}},{"ray_origin",{p.x,p.y,p.z}},{"ray_direction",{d.x,d.y,d.z}}});
                                }
                            }
                            triggers[device]=pressed;
                            seenControllers[device]=true;
                        }
                    }else {runtime.overlay->HideOverlay(runtime.hud);controllerValid.fill(false);triggers.fill(false);seenControllers.fill(false);}
                }else{for(auto handle:runtime.world)runtime.overlay->HideOverlay(handle);runtime.overlay->HideOverlay(runtime.hud);}
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(spatial?30:50));
        }
        if(!stopping)throw std::runtime_error("Room controller stopped unexpectedly");
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"Room setup: %s\n",e.what());return 1;}
}
