// Public loader dispatch. Runtime files remain owned by the installed runtime.
#include "native_xr_api.h"
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <filesystem>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace room_xr {
namespace {
template<class T>void symbol(void* module,const char* name,T& out){
    const auto address=dlsym(module,name);if(!address)throw std::runtime_error(std::string("Native graphics entry unavailable: ")+name);
    static_assert(sizeof(out)==sizeof(address));std::memcpy(&out,&address,sizeof(out));
}
template<class T>void entry(XrInstance instance,const char* name,T& out){
    PFN_xrVoidFunction address=nullptr;const auto result=xrGetInstanceProcAddr(instance,name,&address);
    if(XR_FAILED(result)||!address)throw std::runtime_error(std::string("Native XR entry unavailable: ")+name);
    static_assert(sizeof(out)==sizeof(address));std::memcpy(&out,&address,sizeof(out));
}
std::string trustedLoader(const std::string& path){
    char canonical[4096]{};if(path.empty()||path.front()!='/'||!realpath(path.c_str(),canonical))throw std::runtime_error("Native XR loader unavailable");
    const auto name=std::filesystem::path(canonical).filename().string();
    if(name.rfind("libopenxr_loader.so",0)!=0)throw std::runtime_error("Unexpected native XR loader");
    const int fd=open(canonical,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat info{};Elf64_Ehdr elf{};
    const bool valid=fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode)&&!(info.st_mode&0022)
        &&(info.st_uid==0||info.st_uid==getuid())&&info.st_size>=64&&info.st_size<=8*1024*1024
        &&pread(fd,&elf,sizeof(elf),0)==ssize_t(sizeof(elf))&&!std::memcmp(elf.e_ident,ELFMAG,SELFMAG)
        &&elf.e_ident[EI_CLASS]==ELFCLASS64&&elf.e_ident[EI_DATA]==ELFDATA2LSB
#if defined(__aarch64__)
        &&elf.e_machine==EM_AARCH64;
#else
        &&elf.e_machine==EM_X86_64;
#endif
    if(fd>=0)close(fd);
    if(!valid)throw std::runtime_error("Untrusted native XR loader");
    return canonical;
}
}
void bindInstanceApi(XrInstance instance){
    entry(instance,"xrDestroyInstance",xrDestroyInstance);
#define ROOM_BIND_XR(name) entry(instance,#name,name);
    ROOM_XR_FUNCTIONS(ROOM_BIND_XR)
#undef ROOM_BIND_XR
}
NativeXrLibraries::NativeXrLibraries(const std::string& loader){
    try{const auto path=trustedLoader(loader);xr_=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
        if(!xr_)throw std::runtime_error("Native XR loader could not be loaded");
        symbol(xr_,"xrGetInstanceProcAddr",xrGetInstanceProcAddr);
        entry(XR_NULL_HANDLE,"xrCreateInstance",xrCreateInstance);
        entry(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",xrEnumerateInstanceExtensionProperties);
        vk_=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!vk_)throw std::runtime_error("Native Vulkan loader unavailable");
#define ROOM_BIND_VK(name) symbol(vk_,#name,name);
        ROOM_VK_FUNCTIONS(ROOM_BIND_VK)
#undef ROOM_BIND_VK
    }catch(...){if(vk_)dlclose(vk_);if(xr_)dlclose(xr_);vk_=xr_=nullptr;throw;}
}
NativeXrLibraries::~NativeXrLibraries(){if(vk_)dlclose(vk_);if(xr_)dlclose(xr_);}
} // namespace room_xr
