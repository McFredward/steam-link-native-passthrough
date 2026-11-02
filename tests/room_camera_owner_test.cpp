#include "room_scan/camera_api/owner_lifetime.h"
#include <cstdio>
#include <sys/wait.h>
using namespace room_camera_api;
namespace {void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}}
int main(){try{
    unsetenv("SFNP_ROOM_OWNER_PID");unsetenv("SFNP_ROOM_OWNER_START");
    ManualOwner diagnostic{unsigned(getuid())};check(!diagnostic.monitored()&&diagnostic.alive(),"standalone diagnostics have no detached owner");
    int gate[2];check(pipe(gate)==0,"owned test lifetime pipe");const auto child=fork();check(child>=0,"owned child");
    if(child==0){close(gate[1]);char byte;const auto ignored=read(gate[0],&byte,1);(void)ignored;_exit(0);}
    close(gate[0]);const auto pid=std::to_string(child),start=processStartTicks(unsigned(child));
    setenv("SFNP_ROOM_OWNER_PID",pid.c_str(),1);setenv("SFNP_ROOM_OWNER_START",start.c_str(),1);
    ManualOwner owner{unsigned(getuid())};check(owner.monitored()&&owner.alive(),"pidfd observes the exact live session owner");
    setenv("SFNP_ROOM_OWNER_START",(start+"1").c_str(),1);bool refused=false;
    try{ManualOwner changed{unsigned(getuid())};}catch(const std::exception&){refused=true;}
    check(refused,"reused PID cannot inherit manual camera authority");
    close(gate[1]);int status=0;check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"owned test child exits");
    check(!owner.alive(),"owner exit ends reader authority without a signal or expiry timer");
    unsetenv("SFNP_ROOM_OWNER_PID");unsetenv("SFNP_ROOM_OWNER_START");
    std::puts("PASS manual owner identity, pidfd lifetime and no authority after owner exit");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
