// Unprivileged Room Setup consumer of a manually armed read-only camera session.
#pragma once
#include "native_arcturus_geometry.h"
#include "native_camera_client.h"
#include "native_capture_pose.h"
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>

namespace room_native {
inline Json manualCameraSession(){
    const auto root="/run/user/"+std::to_string(getuid())+"/sfnp-room-camera";struct stat directory{};
    if(lstat(root.c_str(),&directory)||!S_ISDIR(directory.st_mode)||directory.st_uid!=getuid()||(directory.st_mode&0077))
        throw std::runtime_error("room-capture-not-armed");
    const int fd=open((root+"/session.json").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct File {int fd;~File(){if(fd>=0)::close(fd);}} owned{fd};
    struct stat info{};if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||(info.st_mode&0077)||info.st_size<=0||info.st_size>4096)
        throw std::runtime_error("room-capture-not-armed");
    std::string body(std::size_t(info.st_size),'\0');if(read(fd,body.data(),body.size())!=ssize_t(body.size()))throw std::runtime_error("room-capture-session-unavailable");
    const auto data=Json::parse(body);if(data.at("schema")!="sfnp-room-camera-session-v1"||data.at("state")!="armed")
        throw std::runtime_error("room-capture-not-armed");
    return data;
}
class NativeArcturusSource {
    CameraClient client_;ArcturusRectification geometry_;bool active_=false;
    std::string address_,lastToken_;Json description_;std::uint64_t sequence_=0,lastExposure_=0;
    std::array<CameraClient::Reply,2> prefetched_{};
    Json hint(const std::string& value)const{return {{"event","capture_hint"},{"message",value}};}
public:
    VR_IVRSystem_FnTable* system=nullptr;Json binding,diagnostics;std::string status;
    std::function<Json()> sessionRecord=manualCameraSession;
    ~NativeArcturusSource(){stop();}
    bool active()const{return active_;}
    void stop(){active_=false;client_.close();lastToken_.clear();prefetched_={};}
    bool start(const std::string& session,const std::string& origin){
        stop();diagnostics={{"source","manual-read-only-arcturus"}};
        try{
            if(!system||!system->GetDeviceToAbsoluteTrackingPose)throw std::runtime_error("head-tracking-unavailable");
            const auto record=sessionRecord();address_=record.at("abstract_socket");
            client_.connect(address_);description_=client_.request({{"command","describe"}}).metadata;
            if(description_.at("schema")!="sfnp-native-camera-api-v1"||!description_.at("setup_privileges_dropped").get<bool>()
                ||description_.at("reader_uid")!=getuid()||!description_.at("maps_read_only").get<bool>()
                ||description_.at("camera_settings_changed").get<bool>()||description_.at("boot_changes").get<bool>())
                throw std::runtime_error("room-capture-contract-invalid");
            const auto& streams=description_.at("streams");std::array<bool,2> roles{};
            if(streams.size()!=2)throw std::runtime_error("room-stereo-inventory-invalid");
            for(const auto& stream:streams){const auto role=stream.at("role").get<std::string>();const int eye=role=="passthrough_left"?0:role=="passthrough_right"?1:-1;
                if(eye<0||roles[unsigned(eye)]||!stream.at("kernel_role_verified").get<bool>())throw std::runtime_error("room-camera-eye-unverified");
                roles[unsigned(eye)]=true;}
            geometry_.configure(arcturusFactoryRig(),boundedJson("/persist/device_config.json"));
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            do{prefetched_=client_.pair();if(!prefetched_[0].pixels.empty())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            while(std::chrono::steady_clock::now()<deadline);
            if(prefetched_[0].pixels.empty())throw std::runtime_error("room-camera-no-live-pair");
            binding={{"session_id",session},{"origin_id",origin},{"calibration_id",stableId(geometry_.descriptor.dump()+"eof-imx616-4360ns-v1")}};
            active_=true;sequence_=lastExposure_=0;status="manual-arcturus-stereo-ready";
            diagnostics["baseline_m"]=geometry_.baseline;diagnostics["reason"]=status;return true;
        }catch(const std::exception& error){status=error.what();diagnostics["reason"]=status;stop();return false;}
    }
    Json next(){
        if(!active_)return {};
        auto eyes=std::move(prefetched_);prefetched_={};if(eyes[0].pixels.empty())eyes=client_.pair(lastToken_);
        if(eyes[0].pixels.empty())return {};
        lastToken_=eyes[0].metadata.at("pair_token");
        for(const auto& eye:eyes){const auto& m=eye.metadata;
            if(m.at("source_session")!=address_||m.at("producer_pid")!=description_.at("producer_pid")
                ||m.at("producer_start_ticks")!=description_.at("producer_start_ticks"))throw std::runtime_error("Room camera producer changed; arm capture again");}
        const std::array<std::optional<ArcturusTiming>,2> timing{arcturusTiming(eyes[0].metadata),arcturusTiming(eyes[1].metadata)};
        if(!timing[0]||!timing[1])return hint("Waiting for stable camera exposure. Keep passthrough visible.");
        const auto first=std::min(timing[0]->firstRowCenter,timing[1]->firstRowCenter),last=std::max(timing[0]->lastRowCenter,timing[1]->lastRowCenter);
        const auto lo=std::min(timing[0]->center,timing[1]->center),hi=std::max(timing[0]->center,timing[1]->center),center=lo+(hi-lo)/2;
        if(center<=lastExposure_)throw std::runtime_error("Room exposure sequence changed");
        CapturePoseSpan poses;
        try{poses.read(system,first,last);}catch(const std::exception&){return hint("Waiting for a fresh tracked camera observation.");}
        // Rotation is corrected by each source row's actual exposure center.
        // Residual translation across the readout has no guessed scene depth.
        if(poses.translationSpan>.0005f||poses.rotationSpan>.003f||poses.queryBudget>750000u)
            return hint("Move more slowly, then hold still briefly to capture detail.");
        const Mat head=poses.at(center);
        std::array<ArcturusRectification::RectifiedImage,2> rectified;
        for(unsigned eye=0;eye<2;++eye)rectified[eye]=geometry_.rectifyTimed(eye,eyes[eye].pixels,*timing[eye],head,poses);
        const std::array<std::vector<std::uint8_t>,2> support{std::move(rectified[0].support),std::move(rectified[1].support)};
        auto stereo=matchStereo(rectified[0].pixels,rectified[1].pixels,220,geometry_.baseline,&support);
        unsigned samples=0;for(unsigned i=0;i<stereo.depth.size();++i)if(stereo.depth[i]>0){++samples;
            // Distant low-disparity measurements remain visibly uncertain.
            if(stereo.depth[i]>2.5f)stereo.confidence[i]=std::min(stereo.confidence[i],.6f);}
        if(!samples)return hint("Look towards textured surfaces to capture more detail.");
        const Mat roomFromCamera=multiply(head,geometry_.headFromRectified);lastExposure_=center;++sequence_;
        return {{"event","depth"},{"binding",binding},{"sequence",sequence_},{"exposure_ticks",center},
            {"pose",{{"position",{roomFromCamera.m[0][3],roomFromCamera.m[1][3],roomFromCamera.m[2][3]}},{"orientation_xyzw",quaternion(roomFromCamera)}}},
            {"width",160},{"height",120},{"intrinsics",{110.,110.,79.75,59.75}},{"depth_m",stereo.depth},{"confidence",stereo.confidence}};
    }
};
} // namespace room_native
