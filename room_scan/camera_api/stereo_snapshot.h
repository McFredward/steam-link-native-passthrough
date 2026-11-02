// A bounded immutable snapshot, bound by actual hardware SOF, not arrival order.
#pragma once
#include "trace_reader.h"
#include <memory>

namespace room_camera_api {
template<class Image>class StereoSnapshots {
    using Pointer=std::shared_ptr<const Image>;
    struct Pair {std::string token;std::array<Pointer,2> eyes;};
    std::array<Pointer,2> pending_{};Pair current_,previous_;std::uint64_t serial_=0;
    static std::uint64_t time(const Pointer& p){return p->metadata.at("capture_metadata").at("sof_raw_ns").template get<std::uint64_t>();}
    static bool fresh(const Pointer& p,std::uint64_t now){
        if(!p)return false;
        const auto end=p->metadata.at("copy_end_ns").template get<std::uint64_t>();
        return now>=end&&now-end<500000000u;
    }
public:
    void add(Image image){
        const auto& m=image.metadata;const std::string role=m.value("role","");
        const int eye=role=="passthrough_left"?0:role=="passthrough_right"?1:-1;
        if(eye<0||!m.value("kernel_role_verified",false)||!m.value("queue_mapping_exact",false)||!m.value("copy_stable",false)
            ||m.value("encoding","")!="nv12-u8-luma-candidate"||m.value("width",0)!=616||m.value("height",0)!=616
            ||image.pixels.size()!=616*616||!m.contains("capture_metadata")||!m["capture_metadata"].is_object())return;
        const auto& timing=m.at("capture_metadata");
        if(timing.value("capture_clock","")!="CLOCK_MONOTONIC_RAW"||!timing.value("v4l_timestamp_matches",false)
            ||!timing.value("previous_sof_raw_ns",std::uint64_t(0)))return;
        pending_[unsigned(eye)]=std::make_shared<Image>(std::move(image));
        if(!pending_[0]||!pending_[1])return;
        const auto& a=pending_[0]->metadata;const auto& b=pending_[1]->metadata;
        for(const auto* key:{"source_session","producer_pid","producer_start_ticks"})if(a.at(key)!=b.at(key))return;
        if(a.at("sensor")==b.at("sensor"))return;
        const auto left=time(pending_[0]),right=time(pending_[1]);
        // 100 us is far below the measured 16.7 ms frame spacing. A skipped
        // eye cannot be mistaken for its neighbour, even if arrival is delayed.
        if((left>right?left-right:right-left)>100000u)return;
        if(current_.eyes[0]&&left<=time(current_.eyes[0]))return;
        const std::string token=std::to_string(++serial_);
        for(auto& p:pending_){auto owned=std::make_shared<Image>(*p);owned->metadata["pair_token"]=token;p=std::move(owned);}
        previous_=std::move(current_);current_={token,pending_};
    }
    Json pair(std::uint64_t now)const{
        if(!fresh(current_.eyes[0],now)||!fresh(current_.eyes[1],now))return {{"status","no-fresh-pair"}};
        Json out={{"schema","sfnp-native-camera-pair-v1"},{"pair_token",current_.token},
            {"source_session",current_.eyes[0]->metadata.at("source_session")},{"members",Json::array()}};
        for(const auto& p:current_.eyes)out["members"].push_back({{"sensor",p->metadata.at("sensor")},{"role",p->metadata.at("role")},
            {"sequence",p->metadata.at("sequence")},{"sof_raw_ns",time(p)}});
        return out;
    }
    const Image* latest(const Json& request,std::uint64_t now)const{
        const auto sensor=request.at("sensor").template get<std::string>(),token=request.value("pair_token","");
        if(token.empty()){
            for(const auto& p:pending_)if(fresh(p,now)&&p->metadata.at("sensor")==sensor)return p.get();
            return nullptr;
        }
        for(const auto* pair:{&current_,&previous_})if(pair->token==token){
            for(const auto& p:pair->eyes)if(fresh(p,now)&&p->metadata.at("sensor")==sensor)return p.get();
        }
        return nullptr;
    }
};
} // namespace room_camera_api
