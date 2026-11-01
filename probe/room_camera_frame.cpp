// Explicit native Frame public-API inventory. No setters or streaming calls.
#include <openvr_capi.h>
#include "room_camera_probe_core.h"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace room_camera_probe;
class Native final:public Api {
public:
    ~Native()override{
        if(started_&&shutdown_)shutdown_();
        if(module_)dlclose(module_);
        if(fd_>=0)close(fd_);
    }
    bool open(const char* path){
        char canonical[4096]{};
        if(!path||path[0]!='/'||!realpath(path,canonical)||std::strcmp(path,canonical))return false;
        const auto* leaf=std::strrchr(path,'/');if(!leaf||std::strcmp(leaf+1,"libopenvr_api.so"))return false;
        fd_=::open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        struct stat before{},after{};Elf64_Ehdr elf{};
        if(fd_<0||fstat(fd_,&before)||!S_ISREG(before.st_mode)||(before.st_mode&0022)
            ||(before.st_uid!=0&&before.st_uid!=getuid())||before.st_size<64||before.st_size>8*1024*1024
            ||pread(fd_,&elf,sizeof(elf),0)!=ssize_t(sizeof(elf))||std::memcmp(elf.e_ident,ELFMAG,SELFMAG)
            ||elf.e_ident[EI_CLASS]!=ELFCLASS64||elf.e_ident[EI_DATA]!=ELFDATA2LSB||elf.e_machine!=EM_AARCH64)return false;
        module_=dlopen(path,RTLD_NOW|RTLD_LOCAL);
        if(!module_||stat(path,&after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino
            ||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec
            ||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec)return false;
        if(!symbol(init_,"VR_InitInternal2")||!symbol(get_,"VR_GetGenericInterface")||!symbol(shutdown_,"VR_ShutdownInternal"))return false;
        EVRInitError error=EVRInitError_VRInitError_None;
        init_(&error,EVRApplicationType_VRApplication_Background,nullptr);
        if(error)return false;
        started_=true;
        if(!table(system_,"FnTable:IVRSystem_026")||!system_->IsTrackedDeviceConnected
            ||!system_->GetTrackedDeviceClass||!system_->GetStringTrackedDeviceProperty
            ||!system_->GetInt32TrackedDeviceProperty||!system_->GetArrayTrackedDeviceProperty)return false;
        if(!system_->IsTrackedDeviceConnected(0)||system_->GetTrackedDeviceClass(0)!=ETrackedDeviceClass_TrackedDeviceClass_HMD)return false;
        char name[128]{};ETrackedPropertyError property=ETrackedPropertyError_TrackedProp_Success;
        const auto bytes=system_->GetStringTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_ModelNumber_String,name,sizeof(name),&property);
        if(property||!bytes||bytes>sizeof(name)||name[bytes-1]||std::memchr(name,0,bytes-1)
            ||(std::strcmp(name,"Steam Frame")&&std::strcmp(name,"Deckard MP")))return false;
        // Missing interface remains useful negative evidence, distinct from
        // absence of an Arcturus module (which this API cannot establish).
        table(camera_,"FnTable:IVRTrackedCamera_006");
        return true;
    }
    void camera(bool& present,int& error)override{
        if(!camera_||!camera_->HasCamera){present=false;error=-1;return;}
        error=int(camera_->HasCamera(0,&present));
    }
    void properties(std::int32_t& count,int& countError,std::int32_t& layout,int& layoutError)override{
        ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
        count=system_->GetInt32TrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_NumCameras_Int32,&error);countError=int(error);
        error=ETrackedPropertyError_TrackedProp_Success;
        layout=system_->GetInt32TrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_CameraFrameLayout_Int32,&error);layoutError=int(error);
    }
    Size size(unsigned kind)override{
        Size out;if(camera_&&camera_->GetCameraFrameSize)
            out.error=int(camera_->GetCameraFrameSize(0,EVRTrackedCameraFrameType(kind),&out.width,&out.height,&out.bytes));
        return out;
    }
    Intrinsics intrinsics(unsigned kind,unsigned index)override{
        Intrinsics out;HmdVector2_t focal{},center{};
        if(camera_&&camera_->GetCameraIntrinsics){
            out.error=int(camera_->GetCameraIntrinsics(0,index,EVRTrackedCameraFrameType(kind),&focal,&center));
            out.fx=focal.v[0];out.fy=focal.v[1];out.cx=center.v[0];out.cy=center.v[1];
        }
        return out;
    }
    void extrinsics(std::array<Extrinsics,MaxCameras>& out)override{
        std::array<HmdMatrix34_t,MaxCameras> matrices{};ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
        const auto bytes=system_->GetArrayTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_CameraToHeadTransforms_Matrix34_Array,
            k_unHmdMatrix34PropertyTag,matrices.data(),sizeof(matrices),&error);
        if(error||bytes>sizeof(matrices)||bytes%sizeof(HmdMatrix34_t))return;
        for(unsigned index=0;index<bytes/sizeof(HmdMatrix34_t);++index){
            static_assert(sizeof(matrices[index])==sizeof(out[index].matrix),"public matrix layout");
            std::memcpy(out[index].matrix.data(),&matrices[index],sizeof(matrices[index]));out[index].available=true;
        }
    }
private:
    template<class T>bool symbol(T& value,const char* name){
        void* address=dlsym(module_,name);static_assert(sizeof(address)==sizeof(value),"public callback ABI");
        std::memcpy(&value,&address,sizeof(value));return value!=nullptr;
    }
    template<class T>bool table(T*& value,const char* name){
        EVRInitError error=EVRInitError_VRInitError_None;value=static_cast<T*>(get_(name,&error));
        if(error)value=nullptr;
        return value!=nullptr;
    }
    using Init=std::uint32_t(*)(EVRInitError*,EVRApplicationType,const char*);
    using Get=void*(*)(const char*,EVRInitError*);
    int fd_=-1;void* module_=nullptr;bool started_=false;
    Init init_=nullptr;Get get_=nullptr;void(*shutdown_)()=nullptr;
    VR_IVRSystem_FnTable* system_=nullptr;VR_IVRTrackedCamera_FnTable* camera_=nullptr;
};
}
int main(int argc,char** argv){
    if(argc!=4||std::strcmp(argv[1],"--inspect")||std::strcmp(argv[2],"--openvr")){
        std::fputs("Usage: sfnp-room-camera-frame --inspect --openvr /canonical/path/libopenvr_api.so\n",stderr);return 2;
    }
    Native native;
    if(!native.open(argv[3])){
        std::fputs("{\"schema\":\"sfnp-room-camera-inventory-v1\",\"status\":\"native-frame-api-unavailable\",\"room_scan_ready\":false}\n",stdout);return 1;
    }
    const auto output=report(inspect(native));
    return std::fwrite(output.data(),1,output.size(),stdout)==output.size()?0:1;
}
