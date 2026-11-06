// Same-user, bounded client for the manually armed read-only camera helper.
// Socket work belongs exclusively on the camera worker, never the renderer.
#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace room_native {
class CameraClient {
    int fd_=-1;
public:
    struct Reply {nlohmann::json metadata;std::vector<std::uint8_t> pixels;};
    CameraClient()=default;CameraClient(const CameraClient&)=delete;CameraClient& operator=(const CameraClient&)=delete;
    ~CameraClient(){close();}
    void close(){if(fd_>=0)::close(fd_);fd_=-1;}
    void connect(const std::string& address){
        close();const auto prefix="sfnp.room.camera."+std::to_string(getuid())+".";
        if(address.size()!=prefix.size()+32||address.rfind(prefix,0)!=0||address.substr(prefix.size()).find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Invalid room camera session address");
        fd_=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd_<0)throw std::runtime_error("Room camera socket unavailable");
        try{
            timeval timeout{0,250000};const int budget=1024*1024;
            if(setsockopt(fd_,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))||setsockopt(fd_,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))
                ||setsockopt(fd_,SOL_SOCKET,SO_RCVBUF,&budget,sizeof(budget)))throw std::runtime_error("Room camera socket budgets unavailable");
            sockaddr_un name{};name.sun_family=AF_UNIX;if(address.size()+1>sizeof(name.sun_path))throw std::runtime_error("Room camera address exceeds budget");
            std::memcpy(name.sun_path+1,address.data(),address.size());
            if(::connect(fd_,reinterpret_cast<sockaddr*>(&name),socklen_t(offsetof(sockaddr_un,sun_path)+1+address.size())))
                throw std::runtime_error("Room capture is not armed for this session");
            ucred peer{};socklen_t size=sizeof(peer);
            if(getsockopt(fd_,SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.uid!=getuid())throw std::runtime_error("Unexpected room camera peer");
        }catch(...){close();throw;}
    }
    Reply request(const nlohmann::json& request){
        const auto body=request.dump();if(fd_<0||body.size()>512)throw std::runtime_error("Invalid room camera request");
        if(send(fd_,body.data(),body.size(),MSG_NOSIGNAL)!=ssize_t(body.size()))throw std::runtime_error("Room camera request failed");
        std::vector<std::uint8_t> data(400400);iovec part{data.data(),data.size()};msghdr message{};message.msg_iov=&part;message.msg_iovlen=1;
        const auto bytes=recvmsg(fd_,&message,0);if(bytes<8||message.msg_flags&MSG_TRUNC)throw std::runtime_error("Room camera response is incomplete");
        unsigned metadata=0,pixels=0;for(unsigned i=0;i<4;++i){metadata|=unsigned(data[i])<<(8*i);pixels|=unsigned(data[4+i])<<(8*i);}
        if(metadata>16384||pixels>384000||8u+metadata+pixels!=unsigned(bytes))throw std::runtime_error("Room camera response extent invalid");
        Reply out{nlohmann::json::parse(data.begin()+8,data.begin()+8+metadata),{data.begin()+8+metadata,data.begin()+bytes}};
        if(pixels&&(out.metadata.value("schema","")!="sfnp-native-camera-frame-v1"||out.metadata.value("width",0)!=616
            ||out.metadata.value("height",0)!=616||pixels!=616*616))throw std::runtime_error("Room camera image geometry invalid");
        return out;
    }
    std::array<Reply,2> pair(const std::string& previousToken="",bool area4=false){
        const auto snapshot=request({{"command","pair"}}).metadata;
        if(snapshot.value("status","")=="no-fresh-pair")return {};
        if(snapshot.at("schema")!="sfnp-native-camera-pair-v1"||snapshot.at("members").size()!=2)throw std::runtime_error("Invalid camera stereo snapshot");
        if(snapshot.at("pair_token")==previousToken)return {};
        std::array<Reply,2> out;
        for(unsigned i=0;i<2;++i){const auto& member=snapshot.at("members").at(i);
            out[i]=request({{"command",area4?"latest_area4":"latest"},{"sensor",member.at("sensor")},{"pair_token",snapshot.at("pair_token")}});
            if(out[i].pixels.empty())return {}; // A replaced bounded snapshot is unavailable, never substituted.
            const auto& m=out[i].metadata;
            for(const auto* key:{"pair_token","source_session"})if(m.at(key)!=snapshot.at(key))throw std::runtime_error("Camera source snapshot changed");
            for(const auto* key:{"sensor","sequence","role"})if(m.at(key)!=member.at(key))throw std::runtime_error("Camera snapshot member changed");
            if(m.at("role")!=(i?"passthrough_right":"passthrough_left")||m.at("capture_metadata").at("sof_raw_ns")!=member.at("sof_raw_ns"))
                throw std::runtime_error("Camera eye or capture time changed");
        }
        return out;
    }
};
} // namespace room_native
