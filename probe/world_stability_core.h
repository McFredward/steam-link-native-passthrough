// Explicit Frame-local reference experiment. No game/stream/settings setters.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace world_stability {
constexpr const char* Schema = "sfnp-frame-world-stability-v1";
constexpr unsigned TextureSize = 256, MaxSamples = 6000, MaxLine = 2048;
constexpr std::size_t MaxOutput = 8 * 1024 * 1024, TerminalReserve = 1024;
constexpr float Distance = 1.2f, Width = .4f;
using Handle = std::uint64_t;
struct Matrix { float m[3][4]{}; };
struct Pixel { std::uint8_t r=0,g=0,b=0,a=0; };
inline bool rigid(const Matrix& pose) {
    for (const auto& row : pose.m) for (float x : row) if (!std::isfinite(x)) return false;
    for (unsigned i=0;i<3;++i) {
        if (std::abs(pose.m[i][3])>10000) return false;
        for (unsigned j=0;j<3;++j) {
            float dot=0; for(unsigned k=0;k<3;++k) dot+=pose.m[i][k]*pose.m[j][k];
            if (std::abs(dot-(i==j?1.f:0.f))>.02f) return false;
        }
    }
    const auto& r=pose.m;
    const float determinant=r[0][0]*(r[1][1]*r[2][2]-r[1][2]*r[2][1])
        -r[0][1]*(r[1][0]*r[2][2]-r[1][2]*r[2][0])+r[0][2]*(r[1][0]*r[2][1]-r[1][1]*r[2][0]);
    return std::abs(determinant-1.f)<.02f;
}
inline Matrix anchor(const Matrix& head) {
    Matrix result=head;
    for(unsigned i=0;i<3;++i) result.m[i][3]-=Distance*head.m[i][2];
    return result;
}
inline std::array<Pixel,TextureSize*TextureSize> pattern() {
    std::array<Pixel,TextureSize*TextureSize> result{};
    for(unsigned y=0;y<TextureSize;++y) for(unsigned x=0;x<TextureSize;++x) {
        const unsigned dx=x>128?x-128:128-x,dy=y>128?y-128:128-y;
        const bool outline=(dx<=4&&dy<=112)||(dy<=4&&dx<=112);
        const bool center=(dx<=1&&dy<=112)||(dy<=1&&dx<=112);
        if(outline) result[y*TextureSize+x]=center?Pixel{255,255,255,255}:Pixel{0,0,0,255};
    }
    return result;
}
enum class Phase : unsigned { Still, Rotation, Translation, Comparison };
enum class Reason : unsigned { Requested, TrackingOriginChanged, RuntimeQuit, OutputFailure, InvalidInput, SampleFailure };
struct Sample {
    std::uint64_t beginMicros=0,endMicros=0;
    Matrix head{},framePose{};
    std::array<float,3> angular{};
    bool headValid=false,frameValid=false,framePoseValid=false,markerVisible=false,dashboard=false,originChanged=false,runtimeQuit=false;
    std::int32_t trackingResult=0,frameOrigin=0;
    std::uint32_t frameIndex=0,presents=0,dropped=0,misPresented=0,reprojectionFlags=0;
    double nativeSystemSeconds=0;
    float intervalMillis=0,gpuMillis=0,transferMillis=0;
};
struct Api {
    virtual ~Api()=default;
    virtual bool initialize()=0;
    virtual bool head(Matrix&)=0;
    virtual bool create(const Matrix&,Handle&)=0;
    virtual bool sample(Sample&)=0;
    virtual bool hide(Handle)=0;
    virtual bool clear(Handle)=0;
    virtual bool destroy(Handle)=0;
    virtual void shutdown()=0;
};
struct Sink { virtual ~Sink()=default; virtual bool write(const std::string&)=0; };
inline std::string prefix(const char* event) {
    return std::string("{\"schema\":\"")+Schema+"\",\"event\":\""+event+"\"";
}
class Output {
public:
    explicit Output(Sink& sink):sink_(sink){}
    bool emit(const std::string& line,bool terminal=false) {
        const auto limit=MaxOutput-(terminal?0:TerminalReserve);
        if(failed_||line.empty()||line.back()!='\n'||line.size()>MaxLine||bytes_>limit||line.size()>limit-bytes_)return false;
        for(unsigned char c:line)if((c<32&&c!='\n')||c>126)return false;
        if(!sink_.write(line)){failed_=true;return false;}
        bytes_+=line.size();return true;
    }
    std::size_t bytes()const{return bytes_;}
    bool failed()const{return failed_;}
private:
    Sink& sink_;std::size_t bytes_=0;bool failed_=false;
};
inline void matrix(std::ostream& out,const Matrix& value,bool valid) {
    out<<'[';
    for(unsigned i=0;i<12;++i){if(i)out<<',';out<<(valid?value.m[i/4][i%4]:0);}
    out<<']';
}
inline bool finiteNumber(double value){return std::isfinite(value)&&std::abs(value)<1e12;}
inline double number(double value,bool& valid){if(!finiteNumber(value)){valid=false;return 0;}return value;}
class Session {
public:
    Session(Api& api,Output& output):api_(api),output_(output){}
    ~Session(){stop(active_?Reason::SampleFailure:Reason::Requested);}
    bool start(bool explicitOptIn) {
        if(!explicitOptIn||attempted_)return false;
        attempted_=true;
        if(!api_.initialize()){refuse(1);stop();return false;}
        Matrix head{};
        if(!api_.head(head)||!rigid(head)){refuse(2);stop();return false;}
        anchor_=anchor(head);
        if(!api_.create(anchor_,handle_)||!handle_){refuse(3);stop();return false;}
        std::ostringstream line;line.imbue(std::locale::classic());line<<std::setprecision(9);
        line<<prefix("ready")<<",\"frame_local\":true,\"native_api_ready\":true,\"headset_visibility_verified\":false,\"standing_anchor\":";
        matrix(line,anchor_,true);line<<",\"width_m\":"<<Width<<",\"distance_m\":"<<Distance<<"}\n";
        if(!output_.emit(line.str())){stop();return false;}
        active_=true;return true;
    }
    bool command(const std::string& text) {
        if(text=="q"||text=="stop"){stop();return true;}
        Phase next;
        if(text=="still")next=Phase::Still;
        else if(text=="yaw")next=Phase::Rotation;
        else if(text=="move")next=Phase::Translation;
        else if(text=="compare")next=Phase::Comparison;
        else {stop(Reason::InvalidInput);return false;}
        phase_=next;
        return true;
    }
    bool tick() {
        if(!active_)return false;
        Sample s{};
        if(!api_.sample(s)){stop(Reason::SampleFailure);return false;}
        if(s.originChanged){stop(Reason::TrackingOriginChanged);return false;}
        if(s.runtimeQuit){stop(Reason::RuntimeQuit);return false;}
        // The overlay transform is never updated from later tracking samples.
        // FrameTiming is public compositor context, not the streamed image's pose.
        if(budget_)return true;
        bool numbers=true;
        const bool head=s.headValid&&rigid(s.head),frame=s.frameValid&&s.framePoseValid&&rigid(s.framePose);
        std::ostringstream line;line.imbue(std::locale::classic());line<<std::setprecision(9)<<std::boolalpha;
        line<<prefix("sample")<<",\"seq\":"<<samples_+1<<",\"phase\":"<<unsigned(phase_)
            <<",\"begin_monotonic_us\":"<<s.beginMicros<<",\"end_monotonic_us\":"<<s.endMicros
            <<",\"head_valid\":"<<head<<",\"tracking_result\":"<<s.trackingResult<<",\"head_standing\":";
        matrix(line,s.head,head);line<<",\"angular_rad_s\":[";
        for(unsigned i=0;i<3;++i){if(i)line<<',';line<<number(s.angular[i],numbers);}line<<']';
        line<<",\"frame_timing_valid\":"<<s.frameValid<<",\"frame_index\":"<<s.frameIndex
            <<",\"presents\":"<<s.presents<<",\"dropped\":"<<s.dropped<<",\"mis_presented\":"<<s.misPresented
            <<",\"reprojection_flags\":"<<s.reprojectionFlags<<",\"native_system_s\":"<<number(s.nativeSystemSeconds,numbers)
            <<",\"client_interval_ms\":"<<number(s.intervalMillis,numbers)<<",\"render_gpu_ms\":"<<number(s.gpuMillis,numbers)
            <<",\"transfer_ms\":"<<number(s.transferMillis,numbers)<<",\"frame_pose_valid\":"<<frame
            <<",\"frame_pose_origin\":"<<s.frameOrigin<<",\"public_frame_pose\":";
        matrix(line,s.framePose,frame);
        line<<",\"marker_visible_api\":"<<s.markerVisible<<",\"dashboard\":"<<s.dashboard
            <<",\"numeric_valid\":"<<numbers<<",\"exact_stream_pose_proven\":false}\n";
        if(line.str().size()+output_.bytes()>MaxOutput-TerminalReserve-256||samples_==MaxSamples){
            budget_=true;
            if(!output_.emit(prefix("budget")+",\"logging_stopped\":true,\"reference_remains_until_stop\":true}\n")){
                stop(Reason::OutputFailure);return false;
            }
            return true;
        }
        if(!output_.emit(line.str())){stop(Reason::OutputFailure);return false;}
        ++samples_;return true;
    }
    bool stop(Reason reason=Reason::Requested) {
        if(!attempted_||stopped_)return clean_;
        stopped_=true;reason_=reason;
        if(handle_){
            clean_=api_.hide(handle_)&&clean_;
            clean_=api_.clear(handle_)&&clean_;
            clean_=api_.destroy(handle_)&&clean_;
        }
        handle_=0;api_.shutdown();
        if(active_)output_.emit(prefix("stopped")+",\"reason\":"+std::to_string(unsigned(reason))+
            ",\"clean\":"+(clean_?"true":"false")+",\"samples\":"+std::to_string(samples_)+"}\n",true);
        active_=false;return clean_;
    }
    bool active()const{return active_;}
    unsigned samples()const{return samples_;}
    Reason reason()const{return reason_;}
private:
    void refuse(unsigned stage){output_.emit(prefix("refused")+",\"stage\":"+std::to_string(stage)+"}\n",true);}
    Api& api_;Output& output_;Handle handle_=0;Matrix anchor_{};Phase phase_=Phase::Still;Reason reason_=Reason::Requested;
    unsigned samples_=0;bool attempted_=false,stopped_=false,active_=false,budget_=false,clean_=true;
};
} // namespace world_stability
