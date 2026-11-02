// A detached manual session is owned by a user process, never a boot service.
// pidfd observes its lifetime without signalling or modifying that process.
#pragma once
#include "linux_inventory.h"
#include <poll.h>
namespace room_camera_api {
inline std::string processStartTicks(unsigned pid){
    const auto text=readText("/proc/"+std::to_string(pid)+"/stat",8192);const auto end=text.rfind(')');
    if(end==std::string::npos)throw std::runtime_error("Manual session process metadata invalid");
    std::istringstream fields(text.substr(end+1));std::string value;
    for(unsigned i=0;i<20;++i)if(!(fields>>value))throw std::runtime_error("Manual session process metadata short");
    return value;
}
class ManualOwner {
    Fd descriptor_;
public:
    explicit ManualOwner(unsigned uid){
        const char* pid=std::getenv("SFNP_ROOM_OWNER_PID"),*start=std::getenv("SFNP_ROOM_OWNER_START");
        if(!pid&&!start)return;
        const std::string p=pid?pid:"",s=start?start:"";
        if(p.empty()||p.size()>10||p.find_first_not_of("0123456789")!=std::string::npos
            ||s.empty()||s.size()>20||s.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("Manual camera owner identity invalid");
        const auto id=std::stoul(p);struct stat info{};const auto path="/proc/"+p;
        if(!id||id>2147483647u||id==unsigned(getpid())||stat(path.c_str(),&info)||info.st_uid!=uid||processStartTicks(unsigned(id))!=s)
            throw std::runtime_error("Manual camera owner changed");
        descriptor_=Fd(int(syscall(SYS_pidfd_open,unsigned(id),0)));
        if(descriptor_.get()<0||processStartTicks(unsigned(id))!=s)throw std::runtime_error("Manual camera owner lifetime unavailable");
    }
    bool monitored()const{return descriptor_.get()>=0;}
    bool alive()const{if(!monitored())return true;pollfd event{descriptor_.get(),POLLIN,0};return poll(&event,1,0)==0;}
};
} // namespace room_camera_api
