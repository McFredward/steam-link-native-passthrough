// Unprivileged Room Setup consumer of a manually armed read-only camera session.
#pragma once
#include "native_arcturus_geometry.h"
#include "native_camera_client.h"
#include "native_capture_pose.h"
#include "native_capture_stats.h"
#include "native_vision.h"
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <sys/stat.h>

namespace room_native {
inline std::string cameraBase64(const std::vector<std::uint8_t>& input){
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((input.size()+2)/3*4);
    for(std::size_t i=0;i<input.size();i+=3){const auto n=std::min(std::size_t(3),input.size()-i);
        const auto v=(unsigned(input[i])<<16)|(n>1?unsigned(input[i+1])<<8:0)|(n>2?input[i+2]:0);
        out+=alphabet[(v>>18)&63];out+=alphabet[(v>>12)&63];out+=n>1?alphabet[(v>>6)&63]:'=';out+=n>2?alphabet[v&63]:'=';}
    return out;
}
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
    bool parked_=false;
    std::array<CameraClient::Reply,2> prefetched_{};
    NativeStereo stereo_;
    CapturePoseHistory poseHistory_;
    CaptureStats stats_;
    Json hint(const std::string& value)const{return {{"event","capture_hint"},{"message",value}};}
public:
    VR_IVRSystem_FnTable* system=nullptr;Json binding,diagnostics;std::string status,visionLibrary;bool pcMode=false;
    std::function<Json()> sessionRecord=manualCameraSession;
    ~NativeArcturusSource(){stop();}
    bool active()const{return active_;}
    void stop(){active_=false;parked_=false;poseHistory_.stop();client_.close();lastToken_.clear();prefetched_={};stereo_.close();}
    void park(){if(active_&&!parked_){client_.request({{"command","park"}});prefetched_={};poseHistory_.stop();parked_=true;}}
    bool start(const std::string& session,const std::string& origin){
        stop();diagnostics={{"source","manual-read-only-arcturus"}};
        try{
            if(!system||!system->GetDeviceToAbsoluteTrackingPose)throw std::runtime_error("head-tracking-unavailable");
            const auto record=sessionRecord();address_=record.at("abstract_socket");
            if(!pcMode)stereo_.open(visionLibrary);
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
            poseHistory_.start(system);
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            do{prefetched_=client_.pair();if(!prefetched_[0].pixels.empty())break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            while(std::chrono::steady_clock::now()<deadline);
            if(prefetched_[0].pixels.empty())throw std::runtime_error("room-camera-no-live-pair");
            binding={{"session_id",session},{"origin_id",origin},{"calibration_id",stableId(geometry_.descriptor.dump()+"eof-imx616-4360ns-v2-bounded-exposure")}};
            active_=true;sequence_=lastExposure_=0;status="manual-arcturus-stereo-ready";
            diagnostics["baseline_m"]=geometry_.baseline;diagnostics["reason"]=status;return true;
        }catch(const std::exception& error){status=error.what();diagnostics["reason"]=status;stop();return false;}
    }
    Json next(){
        if(!active_)return {};
        CaptureStats::Attempt attempt{stats_};
        if(parked_)poseHistory_.start(system);
        parked_=false;
        auto eyes=std::move(prefetched_);prefetched_={};
        {CaptureStats::Measure cost{stats_.costs[CaptureStats::PairRead]};if(eyes[0].pixels.empty())eyes=client_.pair(lastToken_);}
        if(eyes[0].pixels.empty()){attempt.result=CaptureStats::NoPair;return {};}
        lastToken_=eyes[0].metadata.at("pair_token");
        for(const auto& eye:eyes){const auto& m=eye.metadata;
            if(m.at("source_session")!=address_||m.at("producer_pid")!=description_.at("producer_pid")
                ||m.at("producer_start_ticks")!=description_.at("producer_start_ticks"))throw std::runtime_error("Room camera producer changed; arm capture again");}
        const std::array<std::optional<ArcturusTiming>,2> timing{arcturusTiming(eyes[0].metadata),arcturusTiming(eyes[1].metadata)};
        if(!timing[0]||!timing[1]){attempt.result=CaptureStats::Timing;return hint("Synchronizing camera observations. Keep looking around slowly.");}
        const auto first=std::min(timing[0]->firstRowCenter,timing[1]->firstRowCenter),last=std::max(timing[0]->lastRowCenter,timing[1]->lastRowCenter);
        const auto exposureUncertainty=std::max(timing[0]->uncertainty,timing[1]->uncertainty);
        const auto lo=std::min(timing[0]->center,timing[1]->center),hi=std::max(timing[0]->center,timing[1]->center),center=lo+(hi-lo)/2;
        if(center<=lastExposure_)throw std::runtime_error("Room exposure sequence changed");
        CapturePoseSpan poses;
        const auto now=rawClockNs();if(now>=first)stats_.maximumAgeMs=std::max(stats_.maximumAgeMs,double(now-first)*1e-6);
        try{CaptureStats::Measure cost{stats_.costs[CaptureStats::PoseRead]};poses.read(system,first-exposureUncertainty,last+exposureUncertainty,&poseHistory_);}
        catch(const std::exception&){attempt.result=CaptureStats::PoseHistory;return hint("Waiting for a fresh tracked camera observation.");}
        if(poses.sampledHistory)++stats_.cachedSpans;else ++stats_.directSpans;
        stats_.maxQueryNs=std::max(stats_.maxQueryNs,poses.queryBudget);
        // Each ray is rotated and triangulated at its actual exposure-time
        // camera origin. Faster motion still refuses acquisition, rather than
        // substituting a current pose or inventing unobserved geometry.
        if(poses.queryBudget+exposureUncertainty>750000u||!poses.supportsTimeUncertainty(exposureUncertainty)){
            attempt.result=CaptureStats::ClockBudget;return hint("Checking camera timing. Keep looking around slowly.");}
        if(poses.translationSpan>.012f||poses.rotationSpan>.04f){attempt.result=CaptureStats::Motion;
            return hint("Slow down your movement to capture sharper detail.");}
        const Mat head=poses.at(center);
        if(pcMode){
            Json times=Json::array();for(const auto& t:timing)times.push_back({{"first_row_ns",t->firstRowCenter},{"line_ns",t->line},{"integration_ns",t->integration},
                {"uncertainty_ns",t->uncertainty},{"integration_min_ns",t->minimumIntegration},{"integration_max_ns",t->maximumIntegration}});
            Json reference=Json::array();for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)reference.push_back(head.m[r][c]);
            attempt.result=CaptureStats::Accepted;lastExposure_=center;++sequence_;
            return {{"event","stereo_observation"},{"schema","sfnp-room-stereo-v1"},{"binding",binding},{"sequence",sequence_},
                {"exposure_ticks",center},{"pair_token",lastToken_},{"source_session",address_},{"width",616},{"height",616},
                {"encoding","gray8-point-step4"},{"rig",geometry_.exportRig()},{"timing",times},{"poses",poses.exportSamples()},
                {"reference_head",reference},{"images",{cameraBase64(eyes[0].pixels),cameraBase64(eyes[1].pixels)}}};
        }
        std::array<ArcturusRectification::RectifiedImage,2> rectified;
        {CaptureStats::Measure cost{stats_.costs[CaptureStats::Rectify]};
            for(unsigned eye=0;eye<2;++eye)rectified[eye]=geometry_.rectifyTimed(eye,eyes[eye].pixels,*timing[eye],head,poses);}
        const std::array<std::vector<std::uint8_t>,2> support{rectified[0].support,rectified[1].support};
        StereoResult matched;{CaptureStats::Measure cost{stats_.costs[CaptureStats::Stereo]};
            matched=stereo_.match(rectified[0].pixels,rectified[1].pixels,220,geometry_.baseline,support);}
        const Mat roomFromCamera=multiply(head,geometry_.headFromRectified);
        StereoResult stereo;{CaptureStats::Measure cost{stats_.costs[CaptureStats::Triangulate]};
            stereo=exposureAlignedDepth(matched,rectified,roomFromCamera,geometry_.baseline);}
        unsigned samples=0;for(unsigned i=0;i<stereo.depth.size();++i)if(stereo.depth[i]>0){++samples;
            // Distant low-disparity measurements remain visibly uncertain.
            if(stereo.depth[i]>2.5f)stereo.confidence[i]=std::min(stereo.confidence[i],.6f);}
        if(!samples){attempt.result=CaptureStats::NoDepth;return hint("Look towards textured surfaces to capture more detail.");}
        attempt.result=CaptureStats::Accepted;
        lastExposure_=center;++sequence_;
        return {{"event","depth"},{"binding",binding},{"sequence",sequence_},{"exposure_ticks",center},
            {"pose",{{"position",{roomFromCamera.m[0][3],roomFromCamera.m[1][3],roomFromCamera.m[2][3]}},{"orientation_xyzw",quaternion(roomFromCamera)}}},
            {"width",160},{"height",120},{"intrinsics",{110.,110.,79.75,59.75}},{"depth_m",stereo.depth},{"confidence",stereo.confidence}};
    }
};
} // namespace room_native
