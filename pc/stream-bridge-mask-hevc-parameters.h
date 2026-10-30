#pragma once
#include "stream-bridge-mask-hevc.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace stream_bridge::sender {
// NVENC may omit parameter NALs from P outputs even when OUTPUT_SPSPPS is
// requested. Retain only VPS/SPS/PPS bytes from a successfully locked IDR in
// this private encoder epoch and requested group. Never retain image entropy.
// The normative Main/range/extent/slice parser and all frame/image proofs still
// run after this bounded framing operation. Serialized by the encoder owner.
class MaskHevcParameters {
public:
    static constexpr std::size_t Capacity=4,MaximumParameters=4096,MaximumAccessUnit=2u*1024u*1024u;
    enum class Result {Complete,KeyCached,Restored,Refused};
    void reset(std::uint64_t epoch=0)noexcept{epoch_=epoch;groups_={};}
    Result complete(std::vector<std::uint8_t>& au,const MaskHevcPicture& picture,
                    std::size_t maximum=MaximumAccessUnit)noexcept{
        try{
            if(!epoch_||picture.epoch!=epoch_||!picture.sequence||!picture.anchor||picture.anchor>picture.sequence||
                (picture.keyframe?picture.dependency!=0:(picture.dependency<picture.anchor||picture.dependency>=picture.sequence))||
                !maximum||maximum>MaximumAccessUnit||au.empty()||au.size()>maximum)return Result::Refused;
            std::size_t at=0,prefix=0;if(!start(au,0,at,prefix))return Result::Refused;
            for(std::size_t n=0;n<at;++n)if(au[n])return Result::Refused;
            unsigned parameters=0,slices=0,nals=0,pictureType=0;std::size_t parameterBytes=0;
            for(;;){
                if(++nals>7)return Result::Refused;
                const auto body=at+prefix;std::size_t next=0,nextPrefix=0;const bool more=start(au,body,next,nextPrefix);
                const auto end=more?next:au.size();if(end<body+3||(au[body]&0x81)||au[body+1]!=1)return Result::Refused;
                const unsigned type=(au[body]>>1)&63;
                if(type>=32&&type<=34){
                    if(slices||type!=32+parameters)return Result::Refused;
                    ++parameters;parameterBytes=end;if(parameterBytes>MaximumParameters)return Result::Refused;
                }else{
                    if((parameters&&parameters!=3)||(picture.keyframe?(type!=19&&type!=20):type!=1)||
                        (slices&&type!=pictureType)||bool(au[body+2]&128)!=(slices==0)||++slices>4)return Result::Refused;
                    pictureType=type;
                }
                if(!more)break;
                at=next;prefix=nextPrefix;
            }
            if(!slices)return Result::Refused;
            if(parameters==3){
                if(!picture.keyframe)return Result::Complete;
                Group* target=nullptr;
                for(auto& group:groups_)if(group.anchor==picture.anchor){target=&group;break;}
                if(target&&target->sequence>picture.sequence)return Result::Complete; // Never rewind cached state.
                if(!target)for(auto& group:groups_)if(!group.anchor){target=&group;break;}
                if(!target){target=&*std::min_element(groups_.begin(),groups_.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
                    if(target->sequence>picture.sequence)return Result::Complete;}
                target->anchor=picture.anchor;target->sequence=picture.sequence;target->size=parameterBytes;
                std::copy_n(au.data(),parameterBytes,target->bytes.data());return Result::KeyCached;
            }
            if(parameters||picture.keyframe)return Result::Refused;
            const Group* source=nullptr;
            for(const auto& group:groups_)if(group.anchor==picture.anchor&&group.sequence<=picture.dependency&&group.sequence<picture.sequence){source=&group;break;}
            if(!source||source->size>maximum-au.size())return Result::Refused;
            std::vector<std::uint8_t> result;result.reserve(source->size+au.size());
            result.insert(result.end(),source->bytes.begin(),source->bytes.begin()+source->size);
            result.insert(result.end(),au.begin(),au.end());au.swap(result);return Result::Restored;
        }catch(...){return Result::Refused;}
    }
private:
    struct Group {std::uint64_t anchor=0,sequence=0;std::size_t size=0;std::array<std::uint8_t,MaximumParameters> bytes{};};
    std::uint64_t epoch_=0;std::array<Group,Capacity> groups_{};
    static bool start(const std::vector<std::uint8_t>& au,std::size_t from,std::size_t& at,std::size_t& prefix)noexcept{
        for(std::size_t n=from;n+2<au.size();++n)if(!au[n]&&!au[n+1]){
            if(au[n+2]==1){at=n;prefix=3;return true;}
            if(n+3<au.size()&&!au[n+2]&&au[n+3]==1){at=n;prefix=4;return true;}
        }
        return false;
    }
};
} // namespace stream_bridge::sender
