// Local, manually started diagnostic API. An abstract Unix socket leaves no
// endpoint file, service or boot dependency. Every accepted peer has our UID.
#pragma once
#include "linux_inventory.h"
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

namespace room_camera_api {
class LocalApi {
    struct Client {Fd fd;bool frames=false,controlOnly=false,highResolution=false,metadata=false;};
    Fd listener_;std::vector<Client> clients_;std::string address_;bool stopping_=false;
    static std::vector<std::uint8_t> packet(const Json& metadata,const std::vector<std::uint8_t>& pixels){
        const auto text=metadata.dump();if(text.size()>16384||pixels.size()>384000)throw std::runtime_error("Camera API packet exceeds budget");
        std::vector<std::uint8_t> out(8+text.size()+pixels.size());
        for(unsigned i=0;i<4;++i){out[i]=std::uint8_t(text.size()>>(8*i));out[4+i]=std::uint8_t(pixels.size()>>(8*i));}
        std::memcpy(out.data()+8,text.data(),text.size());if(!pixels.empty())std::memcpy(out.data()+8+text.size(),pixels.data(),pixels.size());return out;
    }
public:
    LocalApi(){
        std::array<unsigned char,16> random{};if(getrandom(random.data(),random.size(),0)!=ssize_t(random.size()))throw std::runtime_error("Camera API session randomness unavailable");
        address_="sfnp.room.camera."+std::to_string(getuid())+".";constexpr char hex[]="0123456789abcdef";
        for(auto byte:random){address_+=hex[byte>>4];address_+=hex[byte&15];}
        listener_=Fd(socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0));if(listener_.get()<0)throw std::runtime_error("Camera API socket unavailable");
        sockaddr_un name{};name.sun_family=AF_UNIX;if(address_.size()+1>sizeof(name.sun_path))throw std::runtime_error("Camera API address exceeds budget");
        std::memcpy(name.sun_path+1,address_.data(),address_.size());
        if(bind(listener_.get(),reinterpret_cast<sockaddr*>(&name),socklen_t(offsetof(sockaddr_un,sun_path)+1+address_.size()))||listen(listener_.get(),2))
            throw std::runtime_error("Camera API endpoint unavailable");
    }
    const std::string& address()const{return address_;}
    bool stopping()const{return stopping_;}
    bool framesRequested()const{for(const auto& client:clients_)if(client.frames)return true;return false;}
    bool highResolutionRequested()const{for(const auto& client:clients_)if(client.frames&&client.highResolution)return true;return false;}
    bool metadataRequested()const{for(const auto& client:clients_)if(client.metadata)return true;return false;}
    void wait(){
        // Camera trace waits must not delay local API requests. A parked reader
        // sleeps on its owned sockets; active capture checks fresh trace data
        // every 2 ms without spinning or touching any native service settings.
        std::array<pollfd,4> descriptors{};std::size_t count=0;
        descriptors[count++]={listener_.get(),POLLIN,0};
        for(const auto& client:clients_)descriptors[count++]={client.fd.get(),POLLIN,0};
        if(::poll(descriptors.data(),count,framesRequested()||metadataRequested()?2:25)<0&&errno!=EINTR)
            throw std::runtime_error("Camera API wait failed");
    }
    template<class Describe,class Frame,class Pair,class Metadata>void poll(Describe describe,Frame frame,Pair pair,Metadata metadata){
        // Release disconnected slots before admitting a new control client.
        for(auto it=clients_.begin();it!=clients_.end();){char byte=0;const auto count=recv(it->fd.get(),&byte,1,MSG_PEEK|MSG_DONTWAIT);
            if(count==0||(count<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK))it=clients_.erase(it);else ++it;}
        for(unsigned i=0;i<4;++i){
            Fd accepted(accept4(listener_.get(),nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK));if(accepted.get()<0)break;
            ucred peer{};socklen_t size=sizeof(peer);
            if(getsockopt(accepted.get(),SOL_SOCKET,SO_PEERCRED,&peer,&size)||size!=sizeof(peer)||peer.uid!=getuid()||clients_.size()>=3)continue;
            const int bytes=1024*1024;if(setsockopt(accepted.get(),SOL_SOCKET,SO_SNDBUF,&bytes,sizeof(bytes)))continue;
            clients_.push_back({std::move(accepted),false,clients_.size()>=2});
        }
        for(auto it=clients_.begin();it!=clients_.end();){
            std::array<char,513> input{};const auto count=recv(it->fd.get(),input.data(),input.size(),MSG_DONTWAIT|MSG_TRUNC);
            if(count<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){++it;continue;}
            if(count<=0||count>512){it=clients_.erase(it);continue;}
            std::vector<std::uint8_t> output;
            try{
                const auto request=Json::parse(input.data(),input.data()+count);const auto command=request.at("command").get<std::string>();
                if(command=="describe")output=packet(describe(),{});
                else if(command=="metadata_next"){
                    if(!request.contains("after")||!request.at("after").is_number_unsigned())
                        throw std::runtime_error("An unsigned diagnostic metadata cursor is required");
                    it->metadata=true;output=packet(metadata(request.at("after").get<std::uint64_t>()),{});
                }
                else if((command=="pair"||command=="latest"||command=="latest_area4"||command=="latest_area2_tile")&&it->controlOnly)output=packet({{"status","control-only-connection"}},{});
                else if(command=="pair"){it->frames=true;output=packet(pair(),{});}
                else if(command=="latest"||command=="latest_area4"||command=="latest_area2_tile"){
                    it->frames=true;
                    if(command=="latest_area2_tile")it->highResolution=true;
                    const auto image=frame(request);
                    output=image?packet(image->metadata,image->pixels):packet({{"status","no-fresh-frame"},{"room_scan_ready",false}},{});
                }else if(command=="park"){it->frames=false;it->highResolution=false;it->metadata=false;output=packet({{"status","parked"}},{});}
                else if(command=="stop"){stopping_=true;output=packet({{"status","stopping"}},{});}
                else output=packet({{"status","unknown-command"}},{});
            }catch(const std::exception&){output=packet({{"status","invalid-request"}},{});}
            const auto sent=send(it->fd.get(),output.data(),output.size(),MSG_DONTWAIT|MSG_NOSIGNAL);
            if(sent!=ssize_t(output.size()))it=clients_.erase(it);else ++it;
        }
    }
};
} // namespace room_camera_api
