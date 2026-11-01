// Explicit standalone AArch64 OpenVR background client, outside Valve processes.
#include <openvr_capi.h>
#include "world_stability_core.h"
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {
using namespace world_stability;
volatile std::sig_atomic_t stopping=0;
void stopSignal(int){stopping=1;}
std::uint64_t nowMicros(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return std::uint64_t(t.tv_sec)*1000000+std::uint64_t(t.tv_nsec)/1000;}
Matrix matrix(const HmdMatrix34_t& source){static_assert(sizeof(HmdMatrix34_t)==sizeof(Matrix),"public matrix ABI");Matrix result{};std::memcpy(result.m,source.m,sizeof(result.m));return result;}
class Stdout final:public Sink {
public:bool write(const std::string& line)override{return std::fwrite(line.data(),1,line.size(),stdout)==line.size()&&std::fflush(stdout)==0;}
};
class Native final:public Api {
public:
    explicit Native(std::string path):path_(std::move(path)){}
    ~Native()override{shutdown();}
    bool initialize()override{
        operation_="library-check";
        char canonical[4096]{};
        if(path_.empty()||path_.size()>=sizeof(canonical)||path_[0]!='/'||!realpath(path_.c_str(),canonical)||path_!=canonical)return false;
        const auto slash=path_.find_last_of('/');
        if(slash==std::string::npos||path_.substr(slash+1)!="libopenvr_api.so")return false;
        fd_=open(path_.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
        struct stat before{};Elf64_Ehdr elf{};
        if(fd_<0||fstat(fd_,&before)||!S_ISREG(before.st_mode)||before.st_size<64||before.st_size>8*1024*1024
            ||pread(fd_,&elf,sizeof(elf),0)!=ssize_t(sizeof(elf))||std::memcmp(elf.e_ident,ELFMAG,SELFMAG)
            ||elf.e_ident[EI_CLASS]!=ELFCLASS64||elf.e_ident[EI_DATA]!=ELFDATA2LSB||elf.e_machine!=EM_AARCH64)return false;
        operation_="library-load";
        module_=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!module_)return false;
        struct stat after{};if(stat(path_.c_str(),&after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino)return false;
        operation_="public-exports";
        if(!symbol(init_,"VR_InitInternal2")||!symbol(get_,"VR_GetGenericInterface")||!symbol(shutdown_,"VR_ShutdownInternal"))return false;
        operation_="background-init";
        EVRInitError error=EVRInitError_VRInitError_None;
        init_(&error,EVRApplicationType_VRApplication_Background,nullptr);
        if(error){code_=std::int32_t(error);return false;}initialized_=true;
        if(!table(system_,"FnTable:IVRSystem_026")||!table(compositor_,"FnTable:IVRCompositor_029")
            ||!table(overlay_,"FnTable:IVROverlay_028"))return false;
        operation_="public-functions";
        return system_->GetDeviceToAbsoluteTrackingPose&&system_->PollNextEvent&&system_->GetTrackedDeviceClass
            &&system_->IsTrackedDeviceConnected&&system_->GetStringTrackedDeviceProperty
            &&compositor_->GetFrameTiming&&compositor_->GetTrackingSpace
            &&overlay_->CreateOverlay&&overlay_->DestroyOverlay&&overlay_->HideOverlay&&overlay_->ClearOverlayTexture
            &&overlay_->SetOverlayRaw&&overlay_->SetOverlayAlpha&&overlay_->SetOverlayWidthInMeters
            &&overlay_->SetOverlaySortOrder&&overlay_->GetOverlaySortOrder
            &&overlay_->SetOverlayTransformTrackedDeviceRelative&&overlay_->GetOverlayTransformTrackedDeviceRelative
            &&overlay_->SetOverlayTexelAspect&&overlay_->SetOverlayFlag&&overlay_->SetOverlayInputMethod
            &&overlay_->SetOverlayTransformAbsolute&&overlay_->GetOverlayTransformAbsolute
            &&overlay_->ShowOverlay&&overlay_->IsOverlayVisible&&overlay_->IsDashboardVisible&&frameIdentity();
    }
    bool head(Matrix& value)override{
        operation_="initial-pose";
        // Startup notifications precede this anchor. Later origin changes end
        // the experiment instead of silently moving the frozen reference.
        VREvent_t event{};
        for(unsigned n=0;n<32&&system_->PollNextEvent(&event,sizeof(event));++n)
            if(event.eventType==EVREventType_VREvent_Quit||event.eventType==EVREventType_VREvent_ProcessQuit
                ||event.eventType==EVREventType_VREvent_DriverRequestedQuit)return false;
        TrackedDevicePose_t pose{};system_->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&pose,1);
        value=matrix(pose.mDeviceToAbsoluteTracking);return pose.bDeviceIsConnected&&pose.bPoseIsValid&&rigid(value);
    }
    bool create(const Matrix& at,Mode mode,Handle& owned)override{
        char key[96]{};std::snprintf(key,sizeof key,"sfnp.world-stability.%ld.%llu",long(getpid()),static_cast<unsigned long long>(nowMicros()));
        char name[]="Steam Frame local reference diagnostic";
        VROverlayHandle_t handle=0;
        if(!overlayOk(overlay_->CreateOverlay(key,name,&handle),"overlay-create")||!handle)return false;
        owned=handle;owned_=handle;
        HmdMatrix34_t pose{};std::memcpy(pose.m,at.m,sizeof(pose.m));
        if(!overlayOk(overlay_->SetOverlayAlpha(handle,0),"overlay-initial-alpha")
            ||!overlayOk(overlay_->SetOverlayWidthInMeters(handle,Width),"overlay-width")
            ||!overlayOk(overlay_->SetOverlayTexelAspect(handle,1),"overlay-aspect")
            ||!overlayOk(overlay_->SetOverlaySortOrder(handle,SortOrder),"overlay-sort-order")
            ||!overlayOk(overlay_->SetOverlayFlag(handle,VROverlayFlags_VisibleInDashboard,mode==Mode::HeadVisibility),"overlay-dashboard")
            ||!overlayOk(overlay_->SetOverlayInputMethod(handle,VROverlayInputMethod_None),"overlay-no-input"))return false;
        if(mode==Mode::WorldReference){
            if(!overlayOk(overlay_->SetOverlayTransformAbsolute(handle,ETrackingUniverseOrigin_TrackingUniverseStanding,&pose),"overlay-absolute-pose"))return false;
        }else{
            if(!overlayOk(overlay_->SetOverlayTransformTrackedDeviceRelative(handle,k_unTrackedDeviceIndex_Hmd,&pose),"overlay-head-relative-pose"))return false;
        }
        pixels_=pattern(mode);
        if(!overlayOk(overlay_->SetOverlayRaw(handle,pixels_.data(),TextureSize,TextureSize,sizeof(Pixel)),"overlay-raw-texture")
            ||!overlayOk(overlay_->ShowOverlay(handle),"overlay-show")
            ||!overlayOk(overlay_->SetOverlayAlpha(handle,1),"overlay-reveal"))return false;
        std::uint32_t sort=0;
        if(!overlayOk(overlay_->GetOverlaySortOrder(handle,&sort),"overlay-sort-readback")||sort!=SortOrder)return false;
        HmdMatrix34_t actual{};
        if(mode==Mode::WorldReference){
            ETrackingUniverseOrigin origin=ETrackingUniverseOrigin_TrackingUniverseSeated;
            if(!overlayOk(overlay_->GetOverlayTransformAbsolute(handle,&origin,&actual),"overlay-pose-readback")
                ||origin!=ETrackingUniverseOrigin_TrackingUniverseStanding)return false;
        }else{
            TrackedDeviceIndex_t device=k_unTrackedDeviceIndexInvalid;
            if(!overlayOk(overlay_->GetOverlayTransformTrackedDeviceRelative(handle,&device,&actual),"overlay-head-pose-readback")
                ||device!=k_unTrackedDeviceIndex_Hmd)return false;
        }
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)if(std::abs(actual.m[r][c]-at.m[r][c])>1e-5f)return false;
        return true;
    }
    bool sample(Sample& out)override{
        out.beginMicros=nowMicros();
        VREvent_t event{};
        for(unsigned count=0;count<32&&system_->PollNextEvent(&event,sizeof(event));++count){
            if(event.eventType==EVREventType_VREvent_ChaperoneUniverseHasChanged
                ||event.eventType==EVREventType_VREvent_SeatedZeroPoseReset)out.originChanged=true;
            if(event.eventType==EVREventType_VREvent_Quit||event.eventType==EVREventType_VREvent_ProcessQuit
                ||event.eventType==EVREventType_VREvent_DriverRequestedQuit)out.runtimeQuit=true;
        }
        TrackedDevicePose_t pose{};system_->GetDeviceToAbsoluteTrackingPose(ETrackingUniverseOrigin_TrackingUniverseStanding,0,&pose,1);
        out.head=matrix(pose.mDeviceToAbsoluteTracking);out.headValid=pose.bDeviceIsConnected&&pose.bPoseIsValid;
        out.trackingResult=pose.eTrackingResult;
        for(unsigned i=0;i<3;++i)out.angular[i]=pose.vAngularVelocity.v[i];
        Compositor_FrameTiming timing{};timing.m_nSize=sizeof(timing);
        out.frameValid=compositor_->GetFrameTiming(&timing,0);
        if(out.frameValid){
            out.frameIndex=timing.m_nFrameIndex;out.presents=timing.m_nNumFramePresents;
            out.dropped=timing.m_nNumDroppedFrames;out.misPresented=timing.m_nNumMisPresented;
            out.reprojectionFlags=timing.m_nReprojectionFlags;out.nativeSystemSeconds=timing.m_flSystemTimeInSeconds;
            out.intervalMillis=timing.m_flClientFrameIntervalMs;out.gpuMillis=timing.m_flTotalRenderGpuMs;out.transferMillis=timing.m_flTransferLatencyMs;
            out.framePoseValid=timing.m_HmdPose.bDeviceIsConnected&&timing.m_HmdPose.bPoseIsValid;
            out.framePose=matrix(timing.m_HmdPose.mDeviceToAbsoluteTracking);
            out.frameOrigin=compositor_->GetTrackingSpace();
        }
        out.markerVisible=overlay_->IsOverlayVisible(owned_);out.dashboard=overlay_->IsDashboardVisible();
        out.endMicros=nowMicros();return out.beginMicros&&out.endMicros>=out.beginMicros;
    }
    bool hide(Handle h)override{return overlay_&&overlay_->HideOverlay(h)==0;}
    bool clear(Handle h)override{return overlay_&&overlay_->ClearOverlayTexture(h)==0;}
    bool destroy(Handle h)override{const bool ok=overlay_&&overlay_->DestroyOverlay(h)==0;if(ok)owned_=0;return ok;}
    void shutdown()override{
        if(initialized_&&shutdown_)shutdown_();
        initialized_=false;
        system_=nullptr;compositor_=nullptr;overlay_=nullptr;
        if(module_)dlclose(module_);
        module_=nullptr;
        if(fd_>=0)close(fd_);
        fd_=-1;
    }
    std::int32_t code()const{return code_;}
    const char* operation()const{return operation_;}
private:
    bool overlayOk(EVROverlayError code,const char* operation){operation_=operation;code_=std::int32_t(code);return code==0;}
    bool frameIdentity(){
        operation_="hmd-identity";
        if(!system_->IsTrackedDeviceConnected(0)||system_->GetTrackedDeviceClass(0)!=ETrackedDeviceClass_TrackedDeviceClass_HMD)return false;
        std::array<char,128> name{};ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
        const auto bytes=system_->GetStringTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_ModelNumber_String,name.data(),name.size(),&error);
        if(error||bytes<2||bytes>name.size()||name[bytes-1]||std::memchr(name.data(),0,bytes-1))return false;
        return !std::strcmp(name.data(),"Steam Frame")||!std::strcmp(name.data(),"Deckard MP");
    }
    template<class T>bool symbol(T& value,const char* name){void* address=dlsym(module_,name);static_assert(sizeof(address)==sizeof(value),"public export pointer ABI");std::memcpy(&value,&address,sizeof(value));return value!=nullptr;}
    template<class T>bool table(T*& value,const char* name){operation_=name;EVRInitError error=EVRInitError_VRInitError_None;value=static_cast<T*>(get_(name,&error));code_=std::int32_t(error);return value&&!error;}
    using Init=std::uint32_t(*)(EVRInitError*,EVRApplicationType,const char*);
    using Generic=void*(*)(const char*,EVRInitError*);
    Init init_=nullptr;Generic get_=nullptr;void(*shutdown_)()=nullptr;
    std::string path_;int fd_=-1;void* module_=nullptr;bool initialized_=false;std::int32_t code_=0;const char* operation_="arguments";
    VR_IVRSystem_FnTable* system_=nullptr;VR_IVRCompositor_FnTable* compositor_=nullptr;VR_IVROverlay_FnTable* overlay_=nullptr;
    VROverlayHandle_t owned_=0;std::array<Pixel,TextureSize*TextureSize> pixels_{};
};
}
int main(int argc,char** argv){
    if(argc!=4||(std::strcmp(argv[1],"--reference")&&std::strcmp(argv[1],"--visibility-check"))||std::strcmp(argv[2],"--openvr")){
        std::fprintf(stderr,"Usage: sfnp-world-stability-frame (--reference | --visibility-check) --openvr /absolute/installed/libopenvr_api.so\n");return 2;
    }
#if !defined(__aarch64__)
    std::fprintf(stderr,"This reference must run locally on the AArch64 Frame.\n");return 2;
#endif
    std::signal(SIGINT,stopSignal);std::signal(SIGTERM,stopSignal);std::signal(SIGHUP,stopSignal);std::signal(SIGPIPE,SIG_IGN);
    try{
        Stdout sink;Output output(sink);Native api(argv[3]);Session session(api,output);
        const auto mode=std::strcmp(argv[1],"--reference") ? Mode::HeadVisibility : Mode::WorldReference;
        if(!session.start(true,mode)){std::fprintf(stderr,"Frame reference refused: operation=%s native-code=%d cleanup=%s\n",api.operation(),api.code(),session.stop()?"clean":"failed");return 3;}
        std::string command;unsigned result=0;std::uint64_t nextSample=0;
        while(!stopping&&session.active()){
            const auto now=nowMicros();
            if(now>=nextSample){if(!session.tick())break;nextSample=now+20000;}
            const auto after=nowMicros();
            const int timeout=after>=nextSample?0:int((nextSample-after+999)/1000);
            pollfd input{STDIN_FILENO,POLLIN|POLLHUP,0};const int ready=poll(&input,1,timeout);
            if(ready<0){if(errno==EINTR)continue;result=4;break;}
            if(ready>0&&(input.revents&(POLLERR|POLLNVAL))){result=4;break;}
            if(ready>0&&(input.revents&(POLLIN|POLLHUP))){
                char bytes[128];const auto got=read(STDIN_FILENO,bytes,sizeof(bytes));
                if(got<=0)break;
                for(ssize_t i=0;i<got;++i){
                    if(bytes[i]=='\r')continue;
                    if(bytes[i]=='\n'){
                        if(!session.command(command))result=4;
                        command.clear();if(!session.active())break;
                    }else if(command.size()<32)command+=bytes[i];
                    else{session.stop(Reason::InvalidInput);result=4;break;}
                }
            }
        }
        if(!session.stop())result=5;
        else if(session.reason()==Reason::OutputFailure||session.reason()==Reason::InvalidInput||session.reason()==Reason::SampleFailure)result=4;
        return int(result);
    }catch(...){std::fprintf(stderr,"Frame reference failed; no game/runtime settings were requested.\n");return 6;}
}
