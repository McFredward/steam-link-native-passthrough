// Real local socket/packet/stop test with a clearly synthetic provider.
#include "room_scan/camera_api/read_only_policy.h"
#include "room_scan/camera_api/local_api.h"
#include <cstdio>
#include <optional>
#include <sys/wait.h>
#include <csignal>
using namespace room_camera_api;
namespace {
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
struct FixtureImage {Json metadata;std::vector<std::uint8_t> pixels;};
struct Child {pid_t pid=-1;~Child(){if(pid>0){kill(pid,SIGTERM);waitpid(pid,nullptr,0);}}};
Fd connectTo(const std::string& address){
    Fd socketFd(socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0));sockaddr_un target{};target.sun_family=AF_UNIX;
    std::memcpy(target.sun_path+1,address.data(),address.size());
    if(connect(socketFd.get(),reinterpret_cast<sockaddr*>(&target),socklen_t(offsetof(sockaddr_un,sun_path)+1+address.size())))return Fd{};
    timeval limit{2,0};setsockopt(socketFd.get(),SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit));return socketFd;
}
std::pair<Json,std::vector<std::uint8_t>> request(int fd,const std::string& command){
    check(send(fd,command.data(),command.size(),MSG_NOSIGNAL)==ssize_t(command.size()),"local request sends");
    std::vector<std::uint8_t> packet(400400);const auto n=recv(fd,packet.data(),packet.size(),MSG_TRUNC);
    check(n>=8&&n<=ssize_t(packet.size()),"response fits its bounded packet");unsigned meta=0,bytes=0;
    for(unsigned i=0;i<4;++i){meta|=unsigned(packet[i])<<(8*i);bytes|=unsigned(packet[4+i])<<(8*i);}
    check(meta<=16384&&bytes<=384000&&8u+meta+bytes==unsigned(n),"exact packet extents");
    return {Json::parse(packet.begin()+8,packet.begin()+8+meta),{packet.begin()+8+meta,packet.begin()+n}};
}
}
int main(){try{
    int pipeFds[2]{};check(pipe(pipeFds)==0,"owned announcement pipe");const auto child=fork();check(child>=0,"test child");
    if(child==0){close(pipeFds[0]);dup2(pipeFds[1],1);close(pipeFds[1]);try{
        installReadOnlyPolicy();sealReadOnlyMappings();LocalApi api;
        FixtureImage image{{{"schema","sfnp-native-camera-frame-v1"},{"room_scan_ready",false},{"test_only",true},{"width",616},{"height",616}},std::vector<std::uint8_t>(616*616)};
        for(std::size_t i=0;i<image.pixels.size();++i)image.pixels[i]=std::uint8_t(i*13);
        const auto announcement=api.address()+"\n";check(write(1,announcement.data(),announcement.size())==ssize_t(announcement.size()),"owned announcement");
        while(!api.stopping()){
            api.poll([](){return Json{{"schema","sfnp-native-camera-api-v1"},{"room_scan_ready",false},{"test_only",true}};},
                [&](const std::string& sensor){return sensor=="fixture"?&image:nullptr;});
            usleep(1000);
        }
        _exit(0);
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());_exit(1);}}
    Child owned{child};close(pipeFds[1]);std::string address;char byte=0;
    while(read(pipeFds[0],&byte,1)==1&&byte!='\n')address+=byte;
    close(pipeFds[0]);
    check(address.rfind("sfnp.room.camera.",0)==0&&address.size()<100,"unpredictable abstract session endpoint");
    auto socket=connectTo(address);check(socket.get()>=0,"same-user API connection");
    auto result=request(socket.get(),R"({"command":"describe"})");check(!result.first.at("room_scan_ready").get<bool>()&&result.second.empty(),"capability handshake cannot invent scan readiness");
    result=request(socket.get(),R"({"command":"latest","sensor":"fixture"})");check(result.second.size()==616*616,"real socket carries the largest diagnostic image");
    for(std::size_t i=0;i<result.second.size();++i)check(result.second[i]==std::uint8_t(i*13),"exact pixel bytes survive transport");
    check(request(socket.get(),R"({"command":"latest","sensor":"missing"})").first.at("status")=="no-fresh-frame","unavailable frame stays unavailable");
    check(request(socket.get(),R"({"command":"set-camera-format"})").first.at("status")=="unknown-command","API has no camera-setting command");
    check(request(socket.get(),"{").first.at("status")=="invalid-request","malformed input remains bounded");
    const auto python=fork();check(python>=0,"Python client test process");
    if(!python){
        execlp("python3","python3","-B","-c",
            "import sys;from probe.room_camera_api_client import Client;c=Client(sys.argv[1]);m,p=c.request('latest',sensor='fixture');assert len(p)==616*616 and m['room_scan_ready'] is False;c.close()",
            address.c_str(),static_cast<char*>(nullptr));_exit(1);
    }
    int pythonStatus=0;check(waitpid(python,&pythonStatus,0)==python&&WIFEXITED(pythonStatus)&&!WEXITSTATUS(pythonStatus),"actual Python client validates a real image packet");
    check(request(socket.get(),R"({"command":"stop"})").first.at("status")=="stopping","stop addresses only this helper");
    int status=0;check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"manual helper stops cleanly");
    owned.pid=-1;
    check(connectTo(address).get()<0,"API endpoint disappears when its process exits, with no filesystem cleanup");
    std::puts("PASS actual local API, read-only kernel policy, exact image packet, invalid commands and endpoint removal on stop");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
