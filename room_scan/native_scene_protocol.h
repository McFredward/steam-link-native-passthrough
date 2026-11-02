// Private, bounded IPC and measured OpenVR/OpenXR origin registration.
#pragma once
#include "native_math.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <sys/socket.h>
#include <cerrno>

namespace room_native {
inline constexpr const char* SceneSchema="sfnp-room-scene-v1";
inline constexpr std::size_t ScenePacketLimit=16384;
inline float rotationDistance(const Mat& a,const Mat& b){
    float trace=0;for(unsigned c=0;c<3;++c)trace+=dot(column(a,c),column(b,c));
    return std::acos(std::clamp((trace-1)/2,-1.f,1.f));
}
inline nlohmann::json sceneMatrix(const Mat& pose){
    if(!rigid(pose))throw std::runtime_error("Invalid room scene transform");
    nlohmann::json out=nlohmann::json::array();for(const auto& row:pose.m)out.push_back({row[0],row[1],row[2],row[3]});return out;
}
inline Mat sceneMatrix(const nlohmann::json& value){
    if(!value.is_array()||value.size()!=3)throw std::runtime_error("Invalid room scene matrix extent");
    Mat out;for(unsigned r=0;r<3;++r){if(!value[r].is_array()||value[r].size()!=4)throw std::runtime_error("Invalid room scene matrix row");
        for(unsigned c=0;c<4;++c)out.m[r][c]=value[r][c].get<float>();}
    if(!rigid(out))throw std::runtime_error("Invalid room scene matrix");
    return out;
}
inline bool sendScenePacket(int fd,nlohmann::json value){
    value["schema"]=SceneSchema;const auto line=value.dump();
    if(line.size()>ScenePacketLimit)throw std::runtime_error("Room scene control exceeds budget");
    const auto sent=send(fd,line.data(),line.size(),MSG_DONTWAIT|MSG_NOSIGNAL);
    if(sent==ssize_t(line.size()))return true;
    if(sent<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return false;
    throw std::runtime_error("Room scene control disconnected");
}
inline std::optional<nlohmann::json> receiveScenePacket(int fd){
    std::array<char,ScenePacketLimit> bytes{};const auto size=recv(fd,bytes.data(),bytes.size(),MSG_DONTWAIT|MSG_TRUNC);
    if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return {};
    if(size<=0||std::size_t(size)>bytes.size())throw std::runtime_error("Invalid room scene control packet");
    auto value=nlohmann::json::parse(bytes.data(),bytes.data()+size);
    if(!value.is_object()||value.value("schema","")!=SceneSchema)throw std::runtime_error("Unknown room scene control schema");
    return value;
}
class SceneOriginRegistration {
    std::array<Mat,3> observations_{};unsigned count_=0;std::uint64_t last_=0;Mat result_{};bool ready_=false;
public:
    bool ready()const{return ready_;}
    const Mat& roomFromStage()const{if(!ready_)throw std::runtime_error("Room scene origin is not verified");return result_;}
    void reset(){count_=0;last_=0;ready_=false;}
    bool observe(const Mat& roomFromHead,const Mat& stageFromHead,std::uint64_t rawTime,std::uint64_t uncertaintyNs){
        if(!rigid(roomFromHead)||!rigid(stageFromHead)||!rawTime||rawTime<=last_||uncertaintyNs>750000)
            throw std::runtime_error("Unverified room scene origin observation");
        if(last_&&rawTime-last_<20000000)return ready_;
        const Mat candidate=multiply(roomFromHead,inverse(stageFromHead));
        const Mat& reference=ready_?result_:count_?observations_[0]:candidate;
        if(length(position(candidate)-position(reference))>.003f||rotationDistance(candidate,reference)>.003f){
            if(ready_)throw std::runtime_error("Room scene origin changed");
            count_=0;
        }
        last_=rawTime;
        if(ready_)return true;
        observations_[count_++]=candidate;
        if(count_<observations_.size())return false;
        V3 center{};std::array<float,4> average{};const auto first=quaternion(observations_[0]);
        for(const auto& observation:observations_){center=center+position(observation);const auto q=quaternion(observation);
            float sign=0;for(unsigned i=0;i<4;++i)sign+=first[i]*q[i];
            for(unsigned i=0;i<4;++i)average[i]+=q[i]*(sign<0?-1.f:1.f);}
        float norm=0;for(float v:average)norm+=v*v;norm=std::sqrt(norm);for(auto& v:average)v/=norm;
        result_=pose(center*(1.f/observations_.size()),average);ready_=true;return true;
    }
};
} // namespace room_native
