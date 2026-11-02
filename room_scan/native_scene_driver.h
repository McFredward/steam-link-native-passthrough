// Coordinate the native scene without moving capture or input out of Room Setup.
#pragma once
#include "native_scene_client.h"
#include "native_capture_pose.h"
#include <functional>

namespace room_native {
class RoomSceneDriver {
    RoomSceneClient child_;SceneOriginRegistration registration_;VR_IVRSystem_FnTable* system_=nullptr;
    std::string executable_,loader_,origin_;bool wanted_=false,failed_=false,ready_=false;
    std::uint64_t revision_=UINT64_MAX;bool sentAllowed_=false;double lastHint_=0;
    const char* originStatus_="waiting";float eyePositionError_=0,eyeRotationError_=0;
    std::uint64_t queryUncertainty_=0;unsigned originReports_=0;
    void failed(){
        if(failed_)return;
        failed_=true;ready_=false;wanted_=false;child_.requestStop();if(unavailable)unavailable();
    }
    void observe(const nlohmann::json& packet){
        originStatus_="invalid-probe";
        if(packet.at("eyes").size()!=2)return;
        const auto raw=packet.at("raw_ns").get<std::uint64_t>(),uncertainty=packet.at("uncertainty_ns").get<std::uint64_t>();
        originStatus_="clock-budget";queryUncertainty_=uncertainty;if(uncertainty>750000)return;
        originStatus_="tracking-history";
        const auto sample=capturePose(system_,raw);
        const Mat left=fromNative(system_->GetEyeToHeadTransform(EVREye_Eye_Left));
        const Mat right=fromNative(system_->GetEyeToHeadTransform(EVREye_Eye_Right));
        const auto done=rawClockNs();originStatus_="clock-budget";
        queryUncertainty_=done>=sample.queryBeforeRaw?done-sample.queryBeforeRaw+uncertainty:UINT64_MAX;
        if(queryUncertainty_>750000)return;
        const Mat stageLeft=sceneMatrix(packet.at("eyes")[0]),stageRight=sceneMatrix(packet.at("eyes")[1]);
        const Mat roomLeft=multiply(sample.head,left),roomRight=multiply(sample.head,right);
        const Mat registrationLeft=multiply(roomLeft,inverse(stageLeft)),registrationRight=multiply(roomRight,inverse(stageRight));
        originStatus_="bilateral-registration";
        eyePositionError_=length(position(registrationLeft)-position(registrationRight));
        eyeRotationError_=rotationDistance(registrationLeft,registrationRight);
        if(!rigid(left)||!rigid(right)||eyePositionError_>.002f||eyeRotationError_>.002f)return;
        originStatus_="settling";
        const bool before=registration_.ready();
        try{registration_.observe(roomLeft,stageLeft,raw,done-sample.queryBeforeRaw+uncertainty);}
        catch(const std::exception&){
            originStatus_="origin-disagreement";
            if(before){registration_.reset();if(originChanged)originChanged();}
            return;
        }
        if(registration_.ready())originStatus_="verified";
        if(!before&&registration_.ready())std::fprintf(stderr,"Room scene: both eye origins verified across three timed observations\n");
    }
public:
    struct Controller {Mat aim,grip;bool pressed=false,valid=false;double updated=0;};
    std::array<Controller,2> controllers;
    std::function<void()> unavailable,originChanged,exited;
    std::function<void(const std::string&)> hint;
    RoomSceneDriver(VR_IVRSystem_FnTable* system,std::string executable,std::string loader)
        :system_(system),executable_(std::move(executable)),loader_(std::move(loader)){}
    bool available()const{return trustedSceneExecutable(executable_);}
    bool running()const{return child_.running();}
    bool ownsPid(std::uint32_t pid)const{return child_.ownsPid(pid);}
    bool ready()const{return ready_&&!failed_&&!child_.stopping();}
    bool aligned()const{return registration_.ready()&&ready();}
    void requestStart(){wanted_=true;failed_=false;}
    void requestStop(){wanted_=false;ready_=false;child_.requestStop();}
    void tick(const std::string& origin,std::shared_ptr<const nlohmann::json> state,double now){
        if(origin!=origin_){origin_=origin;registration_.reset();}
        const bool expected=child_.stopping();
        if(child_.pollExit()){
            ready_=false;registration_.reset();revision_=UINT64_MAX;
            if(!expected)failed();
            if(exited)exited();
        }
        if(wanted_&&!failed_&&!child_.running()){
            if(!child_.start(executable_,loader_)){failed();return;}
            ready_=false;registration_.reset();revision_=UINT64_MAX;sentAllowed_=false;
        }
        if(!child_.running()||child_.stopping())return;
        try{
            for(unsigned n=0;n<32;++n){const auto packet=child_.receive();if(!packet)break;
                const auto kind=packet->value("event","");
                if(kind=="ready")ready_=true;
                else if(kind=="controllers"){
                    for(auto& controller:controllers)controller.valid=false;
                    if(!registration_.ready()||packet->value("origin_id","")!=origin_)continue;
                    const auto& hands=packet->at("hands");if(!hands.is_array()||hands.size()>2)throw std::runtime_error("Invalid native room hands");
                    for(const auto& hand:hands){const auto index=hand.at("hand").get<unsigned>();if(index>1)throw std::runtime_error("Invalid native room hand");
                        controllers[index]={multiply(registration_.roomFromStage(),sceneMatrix(hand.at("aim"))),
                            multiply(registration_.roomFromStage(),sceneMatrix(hand.at("grip"))),hand.at("pressed"),true,now};}
                }
                else if(kind=="error"){failed();return;}
                else if(kind=="origin_changed"){registration_.reset();if(originChanged)originChanged();}
                else if(kind=="origin_probe"){
                    try{observe(*packet);}catch(const std::exception&){/* A delayed observation does not establish an origin. */}
                }
                else throw std::runtime_error("Unexpected room scene reply");
            }
            if(child_.writeFailed()){failed();return;}
            if(state){const auto revision=state->value("world_revision",std::uint64_t(0));const auto& room=state->at("room");
                const bool allowed=!room.is_null()&&room.value("world_visuals_allowed",false);
                if(revision_!=revision||sentAllowed_!=allowed){child_.snapshot(state);revision_=revision;sentAllowed_=allowed;}}
            if(ready_&&!registration_.ready()&&now-lastHint_>1){lastHint_=now;
                if(originReports_++<30)std::fprintf(stderr,"Room scene origin: status=%s uncertainty_us=%llu eye_delta_mm=%.3f eye_rotation_mrad=%.3f\n",
                    originStatus_,static_cast<unsigned long long>(queryUncertainty_/1000),double(eyePositionError_*1000),double(eyeRotationError_*1000));
                if(hint)hint("Hold still briefly to align the room preview.");}
        }catch(const std::exception&){failed();}
    }
    void view(bool allowed,const nlohmann::json& pointers){
        if(!child_.running()||child_.stopping())return;
        const bool verified=allowed&&aligned();
        try{child_.control({{"event","view"},{"origin_id",origin_},{"valid",verified},
            {"stage_from_room",verified?sceneMatrix(inverse(registration_.roomFromStage())):nlohmann::json(nullptr)},
            {"pointers",verified?pointers:nlohmann::json::array()}});}
        catch(const std::exception&){failed();}
    }
};
} // namespace room_native
