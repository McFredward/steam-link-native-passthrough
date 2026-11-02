// Load only the owned private vision module; no system package installation.
#pragma once
#include "native_camera.h"
#include "vision_api.h"
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace room_native {
class NativeStereo {
    void* module_=nullptr;void* context_=nullptr;
    decltype(&sfnp_stereo_create) create_=nullptr;decltype(&sfnp_stereo_destroy) destroy_=nullptr;
    decltype(&sfnp_stereo_compute) compute_=nullptr;
    template<class T>void entry(const char* name,T& out){const auto address=dlsym(module_,name);
        if(!address)throw std::runtime_error("room-vision-unavailable");
        std::memcpy(&out,&address,sizeof(out));}
public:
    NativeStereo()=default;NativeStereo(const NativeStereo&)=delete;NativeStereo& operator=(const NativeStereo&)=delete;
    void close(){if(context_){destroy_(context_);context_=nullptr;}}
    void open(const std::string& path){
        close();
        if(!module_){
            const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct File {int fd;~File(){if(fd>=0)::close(fd);}} file{fd};
            struct stat info{};Elf64_Ehdr elf{};
            bool valid=!path.empty()&&path.front()=='/'&&fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode)&&!(info.st_mode&0022)
                &&(info.st_uid==getuid()||info.st_uid==0)&&info.st_size>=64&&info.st_size<=32*1024*1024
                &&pread(fd,&elf,sizeof(elf),0)==ssize_t(sizeof(elf))&&!std::memcmp(elf.e_ident,ELFMAG,SELFMAG)
                &&elf.e_ident[EI_CLASS]==ELFCLASS64&&elf.e_ident[EI_DATA]==ELFDATA2LSB&&elf.e_type==ET_DYN
#if defined(__aarch64__)
                &&elf.e_machine==EM_AARCH64;
#else
                &&elf.e_machine==EM_X86_64;
#endif
            if(!valid)throw std::runtime_error("room-vision-unavailable");
            module_=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);struct stat after{};
            try{if(!module_||lstat(path.c_str(),&after)||info.st_ino!=after.st_ino||info.st_dev!=after.st_dev)throw std::runtime_error("room-vision-unavailable");
                decltype(&sfnp_room_vision_abi) abi=nullptr;entry("sfnp_room_vision_abi",abi);if(abi()!=1)throw std::runtime_error("room-vision-unavailable");
                entry("sfnp_stereo_create",create_);entry("sfnp_stereo_destroy",destroy_);entry("sfnp_stereo_compute",compute_);
            }catch(...){if(module_)dlclose(module_);module_=nullptr;throw;}
        }
        context_=create_();if(!context_)throw std::runtime_error("room-vision-unavailable");
    }
    StereoResult match(const std::vector<uint8_t>& left,const std::vector<uint8_t>& right,float focal,float baseline,
                        const std::array<std::vector<uint8_t>,2>& support){
        if(!context_||left.size()!=320*240||right.size()!=320*240||support[0].size()!=320*240||support[1].size()!=320*240)
            throw std::runtime_error("Invalid native stereo observation");
        StereoResult result;
        if(compute_(context_,left.data(),right.data(),support[0].data(),support[1].data(),focal,baseline,result.depth.data(),result.confidence.data())<0)
            throw std::runtime_error("Native stereo computation failed");
        return result;
    }
    ~NativeStereo(){close();if(module_)dlclose(module_);}
};
} // namespace room_native
