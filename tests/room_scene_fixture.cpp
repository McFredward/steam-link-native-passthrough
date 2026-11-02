// An owned IPC fixture. No VR, camera, service or external process access.
#include "room_scan/native_scene_protocol.h"
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
using namespace room_native;
int main(){try{
    constexpr int channel=198;fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK);
    sendScenePacket(channel,{{"event","fixture_ready"}});std::string pending;
    for(;;){
        for(unsigned n=0;n<16;++n){const auto packet=receiveScenePacket(channel);if(!packet)break;
            if(packet->value("event","")=="stop")return 0;
            sendScenePacket(channel,{{"event","control_ack"}});}
        std::array<char,8192> bytes{};const auto count=read(STDIN_FILENO,bytes.data(),bytes.size());
        if(count==0)return 0;
        if(count>0){pending.append(bytes.data(),std::size_t(count));if(pending.size()>16*1024*1024)return 2;
            const auto end=pending.find('\n');if(end!=std::string::npos){
                const auto state=nlohmann::json::parse(pending.begin(),pending.begin()+std::ptrdiff_t(end));
                sendScenePacket(channel,{{"event","snapshot_ack"},{"revision",state.at("world_revision")}});pending.erase(0,end+1);}}
        pollfd descriptors[2]{{channel,POLLIN,0},{STDIN_FILENO,POLLIN,0}};poll(descriptors,2,5);
    }
}catch(const std::exception&){return 3;}}
