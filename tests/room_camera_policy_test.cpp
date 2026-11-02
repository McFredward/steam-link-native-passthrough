// Execute the real kernel filter in a disposable child, with owned host files.
#include "room_scan/camera_api/read_only_policy.h"
#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace room_camera_api;
namespace {
void check(bool good,const char* why){if(!good)throw std::runtime_error(why);}
template<class Call>void blocked(Call call,const char* why){errno=0;check(call()==-1&&errno==EPERM,why);}
}
int main(){
    char path[]="/tmp/sfnp-camera-policy-XXXXXX";const int fixture=mkstemp(path);
    if(fixture<0)return 1;
    const char original[]="owned policy fixture";
    if(write(fixture,original,sizeof(original))!=ssize_t(sizeof(original)))return 1;
    const pid_t child=fork();
    if(child==0){try{
        installReadOnlyPolicy();
        const int reader=open(path,O_RDONLY|O_CLOEXEC);check(reader>=0,"read-only metadata open remains available");close(reader);
        blocked([&](){return open(path,O_WRONLY);},"write opens are rejected");
        blocked([&](){return open(path,O_RDWR|O_TRUNC);},"truncate opens are rejected");
        blocked([&](){return write(fixture,"bad",3);},"inherited writable descriptors cannot change files");
        blocked([&](){return unlink(path);},"file removal is rejected");
        blocked([&](){return ioctl(-1,VIDIOC_STREAMON,nullptr);},"stream start cannot reach a camera driver");
        blocked([&](){return ioctl(-1,VIDIOC_STREAMOFF,nullptr);},"stream stop cannot reach a camera driver");
        blocked([&](){return ioctl(-1,VIDIOC_S_FMT,nullptr);},"camera format setters cannot reach a driver");
        errno=0;check(ioctl(-1,VIDIOC_G_FMT,nullptr)==-1&&errno==EBADF,"format readers pass the policy");
        blocked([&](){return syscall(SYS_reboot,0,0,0,0);},"boot operations are rejected");
        blocked([&](){return syscall(SYS_ptrace,0,0,0,0);},"ptrace cannot stop or change XRService");
        blocked([&](){return syscall(SYS_kill,getpid(),0);},"process signalling is rejected");
        blocked([&](){return syscall(SYS_execve,"/does-not-exist",nullptr,nullptr);},"external programs cannot run");
        blocked([&](){return socket(AF_INET,SOCK_STREAM,0);},"network sockets are rejected");
        const int local=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);check(local>=0,"ephemeral local API sockets remain available");close(local);
        sealReadOnlyMappings();
        errno=0;check(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fixture,0)==MAP_FAILED&&errno==EPERM,"shared images cannot be mapped writable after setup");
        void* heap=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);check(heap!=MAP_FAILED,"ordinary private working memory remains available");
        void* image=mmap(nullptr,4096,PROT_READ,MAP_SHARED,fixture,0);check(image!=MAP_FAILED,"read-only image map remains available");
        blocked([&](){return mprotect(image,4096,PROT_READ|PROT_WRITE);},"read-only images cannot be made writable");munmap(image,4096);munmap(heap,4096);
        _exit(0);
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());_exit(1);}}
    int status=0;const auto waited=waitpid(child,&status,0);lseek(fixture,0,SEEK_SET);char current[sizeof(original)]{};
    const auto bytes=read(fixture,current,sizeof(current));close(fixture);unlink(path);
    if(waited!=child||!WIFEXITED(status)||WEXITSTATUS(status)||bytes!=ssize_t(sizeof(original))||std::memcmp(current,original,sizeof(original)))return 1;
    std::puts("PASS real seccomp: camera setters, file mutation, process control, boot changes and network access blocked; metadata readers and local API allowed");return 0;
}
