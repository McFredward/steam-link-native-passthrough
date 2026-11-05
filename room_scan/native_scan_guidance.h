// Head-responsive scan instructions. These UI directions do not fill a room mesh.
#pragma once
#include "native_scene_mesh.h"
#include <nlohmann/json.hpp>

namespace room_native {
class ScanGuidance {
    std::string id_;bool active_=false;std::array<unsigned,72> seen_{},measured_{};
    static constexpr float pi=3.14159265358979323846f;
    static unsigned sector(V3 ray){const auto direction=normalized(ray);
        float yaw=std::atan2(direction.x,-direction.z)+pi;if(yaw>=2*pi)yaw-=2*pi;
        return std::min(11u,unsigned(yaw*12/(2*pi)))+12*std::min(5u,unsigned((std::asin(std::clamp(direction.y,-1.f,1.f))+pi/2)*6/pi));}
    static V3 direction(unsigned i){const auto yaw=(float(i%12)+.5f)*2*pi/12-pi,pitch=(float(i/12)+.5f)*pi/6-pi/2;
        return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};}
public:
    void configure(const nlohmann::json& value){
        if(value.is_null()){active_=false;return;}
        const auto id=value.at("id").get<std::string>();
        if(id.size()>80||value.at("observed").size()!=72||value.at("measured").size()!=72)
            throw std::runtime_error("Invalid scan guidance extent");
        if(id!=id_){id_=id;seen_.fill(0);measured_.fill(0);}
        active_=value.at("active").get<bool>();
        for(unsigned i=0;i<72;++i){const auto seen=value.at("observed")[i].get<unsigned>(),measured=value.at("measured")[i].get<unsigned>();
            if(seen>100||measured>100)throw std::runtime_error("Invalid scan guidance coverage");
            seen_[i]=std::max(seen_[i],seen);measured_[i]=std::max(measured_[i],measured);}
    }
    bool active()const{return active_;}
    std::string append(SceneMeshBuilder& builder,const Mat& head,double now){
        if(!active_)return {};
        const auto forward=column(head,2)*-1;
        // Mark what the wearer looked toward immediately, independently of
        // the slower depth result. Seen does not imply measured room coverage.
        seen_[sector(forward)]=100;
        int target=-1;float score=-100;
        for(unsigned i=0;i<72;++i){if(measured_[i]>=75)continue;
            const auto ray=direction(i);
            const float candidate=(seen_[i]?1.f:2.f)-float(measured_[i])*.008f+dot(ray,forward)*.18f+(ray.y>0?.025f:0);
            if(candidate>score){score=candidate;target=int(i);}}
        if(target<0)return "All directions have measurements. Revisit gaps or continue to review.";
        const auto ray=direction(unsigned(target));
        const V3 local{dot(column(head,0),ray),dot(column(head,1),ray),dot(column(head,2),ray)};
        const float yaw=std::atan2(local.x,-local.z),pitch=std::asin(std::clamp(local.y,-1.f,1.f));
        const float x=std::clamp(yaw*.35f,-.38f,.38f),y=std::clamp(pitch*.35f,-.26f,.26f);
        const float length2=std::hypot(x,y),dx=length2>.001f?x/length2:0,dy=length2>.001f?y/length2:1;
        const float pulse=.5f+.5f*std::sin(float(now)*2*pi/2.5f);
        const std::array<float,4> color=seen_[unsigned(target)]?std::array<float,4>{.96f,.72f,.30f,.35f+.35f*pulse}:
            std::array<float,4>{.20f,.96f,.88f,.35f+.35f*pulse};
        for(unsigned i=0;i<3;++i){const float offset=(float(i)-1)*.022f;
            const V3 tip{x+dx*offset,y+dy*offset,-.65f};
            const V3 a=tip+V3{-dx*.014f-dy*.012f,-dy*.014f+dx*.012f,0};
            const V3 b=tip+V3{-dx*.014f+dy*.012f,-dy*.014f-dx*.012f,0};
            builder.line(transform(head,a),transform(head,tip),color,.0012f);
            builder.line(transform(head,b),transform(head,tip),color,.0012f);}
        if(seen_[unsigned(target)])return "Revisit the faint area from a slightly different position. Keep moving slowly.";
        if(pitch>.35f)return "Look up along a ceiling edge. Keep looking around slowly.";
        if(pitch<-.35f)return "Look down and sweep across the floor.";
        return yaw<0?"Look slowly to the left. Follow the pulse into unseen areas.":
                     "Look slowly to the right. Follow the pulse into unseen areas.";
    }
};
} // namespace room_native
