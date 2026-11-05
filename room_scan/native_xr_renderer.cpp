// Native alpha-blended scene. Camera acquisition and dashboard stay in the parent.
#include "native_xr_graphics.h"
#include "native_scene_protocol.h"
#include "native_scene_snapshots.h"
#include "native_capture_pose.h"
#include "native_xr_input.h"
#include "native_scan_guidance.h"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace room_xr {
using namespace room_native;
using Json=nlohmann::json;
namespace {
volatile std::sig_atomic_t interrupted=0;
void signalStop(int){interrupted=1;}
void checked(XrResult value,const char* name){if(XR_FAILED(value))throw std::runtime_error(std::string(name)+" code="+std::to_string(value));}
#define SCENE_XR(call) checked(call,#call)
template<class T>T extension(XrInstance instance,const char* name){
    PFN_xrVoidFunction value=nullptr;SCENE_XR(xrGetInstanceProcAddr(instance,name,&value));
    if(!value)throw std::runtime_error("Native scene extension unavailable");
    T out;static_assert(sizeof(out)==sizeof(value));std::memcpy(&out,&value,sizeof(out));return out;
}
Mat xrPose(const XrPosef& p){Mat out=pose({p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w});
    if(!rigid(out))throw std::runtime_error("Invalid native scene pose");
    return out;}
XrPosef xrPose(const Mat& p){const auto q=quaternion(p);return {{q[0],q[1],q[2],q[3]},{p.m[0][3],p.m[1][3],p.m[2][3]}};}
class Scene {
    XrInstance instance_=XR_NULL_HANDLE;XrSystemId system_=XR_NULL_SYSTEM_ID;XrSession session_=XR_NULL_HANDLE;
    XrSpace stage_=XR_NULL_HANDLE;SceneGraphics graphics_;int channel_=-1;
    bool running_=false,quit_=false,exitRequested_=false,controlValid_=false,focused_=false;SceneInput input_;
    std::string origin_,blockedOrigin_;Mat stageFromRoom_{};Json pointers_=Json::array();
    PFN_xrConvertTimespecTimeToTimeKHR convertTime_=nullptr;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now(),exitAt_{};
    double lastProbe_=-1,lastReport_=0;std::uint64_t frames_=0;unsigned reports_=0;
    bool previouslyVisible_=false;float visibleSince_=0;
    SceneSnapshots snapshots_{start_};SceneMeshBuilder pointerBuilder_;
    ScanGuidance guidance_;std::vector<SceneVertex> basePointers_;double lastGuidance_=-1;
    std::shared_ptr<const PreparedScene> prepared_;std::shared_ptr<const std::vector<SceneVertex>> uploaded_;
    double seconds()const{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start_).count();}
    void createInstance(){
        uint32_t count=0;SCENE_XR(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr));
        if(count>256)throw std::runtime_error("Native scene extension count exceeds budget");
        std::vector<XrExtensionProperties> properties(count,{XR_TYPE_EXTENSION_PROPERTIES});
        SCENE_XR(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,properties.data()));
        const std::array<const char*,3> required{XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME,XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME};
        for(const auto* name:required)if(std::none_of(properties.begin(),properties.end(),[&](const auto& p){return !std::strcmp(p.extensionName,name);}))
            throw std::runtime_error(std::string("Required native scene extension unavailable: ")+name);
        XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};std::strcpy(info.applicationInfo.applicationName,"Room Setup");
        info.applicationInfo.applicationVersion=1;info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
        info.enabledExtensionCount=required.size();info.enabledExtensionNames=required.data();SCENE_XR(xrCreateInstance(&info,&instance_));bindInstanceApi(instance_);
        convertTime_=extension<PFN_xrConvertTimespecTimeToTimeKHR>(instance_,"xrConvertTimespecTimeToTimeKHR");
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        SCENE_XR(xrGetSystem(instance_,&systemInfo,&system_));
        SCENE_XR(xrEnumerateEnvironmentBlendModes(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,nullptr));
        if(count>16)throw std::runtime_error("Native scene blend count exceeds budget");
        std::vector<XrEnvironmentBlendMode> modes(count);SCENE_XR(xrEnumerateEnvironmentBlendModes(instance_,system_,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes.data()));
        if(std::find(modes.begin(),modes.end(),XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)==modes.end())throw std::runtime_error("Native scene passthrough unavailable");
    }
    void controls(){
        bool pointersChanged=false;
        for(unsigned i=0;i<32;++i){const auto packet=receiveScenePacket(channel_);if(!packet)break;
            const auto kind=packet->value("event","");if(kind=="stop"){interrupted=1;continue;}
            if(kind!="view")throw std::runtime_error("Unknown room scene control");
            const auto next=packet->at("origin_id").get<std::string>();if(next.size()!=36)throw std::runtime_error("Invalid room scene origin");
            origin_=next;controlValid_=packet->at("valid").get<bool>()&&origin_!=blockedOrigin_;
            if(controlValid_)stageFromRoom_=sceneMatrix(packet->at("stage_from_room"));
            pointers_=packet->at("pointers");if(!pointers_.is_array()||pointers_.size()>2)throw std::runtime_error("Room scene pointer budget reached");
            guidance_.configure(packet->value("guidance",Json()));
            pointersChanged=true;
        }
        if(pointersChanged){pointerBuilder_.vertices.clear();for(const auto& p:pointers_){
                const auto a=vectorJson(p.at("origin")),b=vectorJson(p.at("target"));
                pointerBuilder_.line(a,b,{.7f,.96f,.93f,.55f});if(p.value("marker",false))pointerBuilder_.marker(b);}
            basePointers_=pointerBuilder_.vertices;
            if(!guidance_.active())graphics_.upload(pointerBuilder_.vertices,true);}
    }
    void probe(){
        const double now=seconds();if(now-lastProbe_<(controlValid_?1.:.1))return;lastProbe_=now;
        // These are paired clock observations, not an assumed RAW/MONOTONIC
        // offset. The parent queries history at this very same RAW instant.
        const auto before=rawClockNs();timespec monotonic{};
        if(clock_gettime(CLOCK_MONOTONIC,&monotonic))return;
        const auto after=rawClockNs();if(after<before||after-before>100000)return;
        XrTime instant=0;if(XR_FAILED(convertTime_(instance_,&monotonic,&instant)))return;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime=instant;locate.space=stage_;XrViewState status{XR_TYPE_VIEW_STATE};
        std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};uint32_t count=0;
        if(XR_FAILED(xrLocateViews(session_,&locate,&status,views.size(),&count,views.data())))return;
        const auto complete=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT|XR_VIEW_STATE_POSITION_TRACKED_BIT|XR_VIEW_STATE_ORIENTATION_TRACKED_BIT;
        const auto done=rawClockNs();if(count!=2||(status.viewStateFlags&complete)!=complete||done<before||done-before>750000)return;
        sendScenePacket(channel_,{{"event","origin_probe"},{"raw_ns",before+(after-before)/2},{"uncertainty_ns",done-before},
            {"eyes",{sceneMatrix(xrPose(views[0].pose)),sceneMatrix(xrPose(views[1].pose))}}});
    }
    void events(){
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};XrResult result=XR_SUCCESS;
        while((result=xrPollEvent(instance_,&event))==XR_SUCCESS){
            if(event.type==XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED){const auto& changed=reinterpret_cast<const XrEventDataSessionStateChanged&>(event);
                focused_=changed.state==XR_SESSION_STATE_FOCUSED;
                if(changed.state==XR_SESSION_STATE_READY){XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};begin.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    SCENE_XR(xrBeginSession(session_,&begin));running_=true;}
                else if(changed.state==XR_SESSION_STATE_STOPPING){SCENE_XR(xrEndSession(session_));running_=false;quit_=true;}
                else if(changed.state==XR_SESSION_STATE_EXITING||changed.state==XR_SESSION_STATE_LOSS_PENDING)quit_=true;
            }else if(event.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING){
                blockedOrigin_=origin_;controlValid_=false;sendScenePacket(channel_,{{"event","origin_changed"}});
            }else if(event.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)quit_=true;
            event={XR_TYPE_EVENT_DATA_BUFFER};
        }
        if(result!=XR_EVENT_UNAVAILABLE)SCENE_XR(result);
    }
    void updateMesh(){
        if(const auto latest=snapshots_.latest();latest&&latest!=prepared_){prepared_=latest;
            if(prepared_->vertices!=uploaded_){graphics_.upload(*prepared_->vertices);uploaded_=prepared_->vertices;}}
    }
    void frame(){
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};XrFrameState frameState{XR_TYPE_FRAME_STATE};SCENE_XR(xrWaitFrame(session_,&wait,&frameState));
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};SCENE_XR(xrBeginFrame(session_,&begin));
        sendScenePacket(channel_,{{"event","controllers"},{"origin_id",origin_},{"hands",input_.sample(session_,stage_,frameState.predictedDisplayTime,focused_&&controlValid_)}});
        std::array<XrCompositionLayerProjectionView,2> projection{{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}}};
        std::array<XrCompositionLayerDepthInfoKHR,2> depth{{{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR},{XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR}}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};layer.space=stage_;layer.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        bool rendered=false;
        if(frameState.shouldRender){XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};locate.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            locate.displayTime=frameState.predictedDisplayTime;locate.space=stage_;XrViewState status{XR_TYPE_VIEW_STATE};
            std::array<XrView,2> views{{{XR_TYPE_VIEW},{XR_TYPE_VIEW}}};uint32_t count=0;SCENE_XR(xrLocateViews(session_,&locate,&status,views.size(),&count,views.data()));
            const auto valid=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
            if(count==2&&(status.viewStateFlags&valid)==valid){
                Mat roomHead;
                if(controlValid_){roomHead=multiply(inverse(stageFromRoom_),xrPose(views[0].pose));
                    const auto right=multiply(inverse(stageFromRoom_),xrPose(views[1].pose));
                    const auto center=(position(roomHead)+position(right))*.5f;
                    roomHead.m[0][3]=center.x;roomHead.m[1][3]=center.y;roomHead.m[2][3]=center.z;
                    snapshots_.focus(roomHead);}
                const bool visible=controlValid_&&prepared_&&prepared_->allowed&&!prepared_->state->at("room").is_null()
                    &&prepared_->state->at("room").value("origin_id","")==origin_;
                if(visible&&!previouslyVisible_)visibleSince_=float(seconds());
                previouslyVisible_=visible;
                graphics_.setScanPulse(visible&&guidance_.active());
                if(visible&&guidance_.active()){
                    pointerBuilder_.vertices=basePointers_;
                    const auto message=guidance_.append(pointerBuilder_,roomHead,seconds());
                    graphics_.upload(pointerBuilder_.vertices,true);
                    if(!message.empty()&&seconds()-lastGuidance_>1){lastGuidance_=seconds();
                        sendScenePacket(channel_,{{"event","guidance"},{"message",message}});}
                }
                graphics_.setRoomPose(xrPose(stageFromRoom_));
                for(unsigned eye=0;eye<2;++eye){uint32_t colorIndex=0,depthIndex=0;XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                    SCENE_XR(xrAcquireSwapchainImage(graphics_.swapchain(eye),&acquire,&colorIndex));
                    SCENE_XR(xrAcquireSwapchainImage(graphics_.depthSwapchain(eye),&acquire,&depthIndex));
                    XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};imageWait.timeout=1000000000;
                    if(xrWaitSwapchainImage(graphics_.swapchain(eye),&imageWait)!=XR_SUCCESS||xrWaitSwapchainImage(graphics_.depthSwapchain(eye),&imageWait)!=XR_SUCCESS)
                        throw std::runtime_error("Native scene image wait did not complete");
                    graphics_.render(eye,colorIndex,depthIndex,views[eye],float(seconds()),visible,visibleSince_);
                    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                    SCENE_XR(xrReleaseSwapchainImage(graphics_.swapchain(eye),&release));SCENE_XR(xrReleaseSwapchainImage(graphics_.depthSwapchain(eye),&release));
                    projection[eye].next=&depth[eye];projection[eye].pose=views[eye].pose;projection[eye].fov=views[eye].fov;
                    projection[eye].subImage.swapchain=graphics_.swapchain(eye);projection[eye].subImage.imageRect.extent={int32_t(graphics_.width(eye)),int32_t(graphics_.height(eye))};
                    depth[eye].subImage=projection[eye].subImage;depth[eye].subImage.swapchain=graphics_.depthSwapchain(eye);
                    depth[eye].minDepth=0;depth[eye].maxDepth=1;depth[eye].nearZ=.05f;depth[eye].farZ=100;
                }
                layer.viewCount=2;layer.views=projection.data();rendered=true;
            }
        }
        const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer)};
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=frameState.predictedDisplayTime;end.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
        end.layerCount=rendered?1:0;end.layers=rendered?layers:nullptr;SCENE_XR(xrEndFrame(session_,&end));if(rendered)++frames_;
    }
public:
    explicit Scene(int channel):channel_(channel){}
    void initialize(){
        createInstance();graphics_.initialize(instance_,system_);XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};info.next=&graphics_.graphicsBinding();info.systemId=system_;
        SCENE_XR(xrCreateSession(instance_,&info,&session_));uint32_t count=0;SCENE_XR(xrEnumerateReferenceSpaces(session_,0,&count,nullptr));
        input_.initialize(instance_,session_);
        if(count>16)throw std::runtime_error("Native scene space count exceeds budget");
        std::vector<XrReferenceSpaceType> spaces(count);SCENE_XR(xrEnumerateReferenceSpaces(session_,count,&count,spaces.data()));
        if(std::find(spaces.begin(),spaces.end(),XR_REFERENCE_SPACE_TYPE_STAGE)==spaces.end())throw std::runtime_error("Native room stage is unavailable");
        XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;space.poseInReferenceSpace.orientation.w=1;
        SCENE_XR(xrCreateReferenceSpace(session_,&space,&stage_));graphics_.createSwapchains(instance_,system_,session_);
        sendScenePacket(channel_,{{"event","ready"}});std::fprintf(stderr,"Room scene: native alpha blend and per-eye depth initialized\n");
    }
    void run(){while(!quit_){events();controls();if(!snapshots_.alive||interrupted){
                if(running_&&!exitRequested_){SCENE_XR(xrRequestExitSession(session_));exitRequested_=true;exitAt_=std::chrono::steady_clock::now();}
                else if(!running_)break;
            }
            if(exitRequested_&&std::chrono::duration<double>(std::chrono::steady_clock::now()-exitAt_).count()>3)break;
            if(quit_)break;
            if(running_){updateMesh();probe();frame();}else std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if(seconds()-lastReport_>10&&reports_++<120){lastReport_=seconds();std::fprintf(stderr,"Room scene: frames=%llu vertices=%zu aligned=%d\n",
                static_cast<unsigned long long>(frames_),uploaded_?uploaded_->size():0,int(controlValid_));}
        }}
    ~Scene(){input_.close();graphics_.destroySwapchains();if(stage_&&xrDestroySpace)xrDestroySpace(stage_);if(session_&&xrDestroySession)xrDestroySession(session_);
        graphics_.shutdown();if(instance_&&xrDestroyInstance)xrDestroyInstance(instance_);}
};
}
} // namespace room_xr
int main(int argc,char** argv){
    using namespace room_xr;int channel=-1;
    try{std::string loader;for(int i=1;i<argc;++i){const std::string key=argv[i];if(i+1>=argc)throw std::runtime_error("Missing room renderer argument");
            const std::string value=argv[++i];if(key=="--openxr")loader=value;else if(key=="--control-fd")channel=std::stoi(value);else throw std::runtime_error("Unknown room renderer argument");}
        if(channel!=198||loader.empty()||geteuid()!=getuid())throw std::runtime_error("An owned unprivileged room renderer connection is required");
        int type=0;socklen_t length=sizeof(type);struct ucred peer{};socklen_t peerLength=sizeof(peer);
        if(getsockopt(channel,SOL_SOCKET,SO_TYPE,&type,&length)||type!=SOCK_SEQPACKET||getsockopt(channel,SOL_SOCKET,SO_PEERCRED,&peer,&peerLength)
            ||peer.uid!=getuid()||peer.pid!=getppid())throw std::runtime_error("Room renderer peer is unverified");
        signal(SIGINT,signalStop);signal(SIGTERM,signalStop);signal(SIGHUP,signalStop);signal(SIGPIPE,SIG_IGN);
        NativeXrLibraries libraries(loader);Scene scene(channel);scene.initialize();scene.run();return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Room scene: %s\n",error.what());
        if(channel>=0)try{room_native::sendScenePacket(channel,{{"event","error"},{"reason","room-renderer-unavailable"}});}catch(const std::exception&){}
        return 1;
    }
}
