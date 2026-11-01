// SetOverlayRaw is an asynchronous image loader, not a video frame queue.
// Retain immutable bytes until its completion event; coalesce at the producer.
#pragma once
#include "native_raster.h"
#include <nlohmann/json.hpp>
#include <string>

namespace room_native {
inline bool sameControls(const nlohmann::json& displayed,const nlohmann::json& current){
    return displayed.at("page")==current.at("page")&&displayed.at("buttons")==current.at("buttons");
}
class RawOverlayImage {
public:
    explicit RawOverlayImage(double minimumInterval=.1):minimumInterval_(minimumInterval){}
    bool pending()const{return pending_;}
    bool loaded()const{return loaded_;}
    std::uint64_t submissions()const{return submissions_;}
    std::uint64_t coalesced()const{return coalesced_;}
    const std::vector<std::uint8_t>& retained()const{return bytes_;}
    bool matches(const Canvas& image)const{return submissions_&&image.width==width_&&image.height==height_&&bytes_==image.pixels;}
    void check(double now,const char* name)const{
        if(pending_&&now-started_>10)throw std::runtime_error(std::string("Overlay image load timed out: ")+name);
    }
    void complete(bool success,const char* name){
        if(!pending_)return;
        pending_=false;
        if(!success)throw std::runtime_error(std::string("Overlay asynchronous image load failed: ")+name);
        loaded_=true;
    }
    template<class Submit>bool update(const Canvas& canvas,double now,const char* name,Submit submit){
        check(now,name);
        if(submissions_&&(canvas.width!=width_||canvas.height!=height_))throw std::runtime_error("Overlay bitmap extent changed");
        if(pending_){++coalesced_;return false;}
        if(matches(canvas))return false;
        if(submissions_&&now-started_<minimumInterval_)return false;
        if(canvas.width<=0||canvas.height<=0||canvas.width>1024||canvas.height>1024
            ||canvas.pixels.size()!=std::size_t(canvas.width)*canvas.height*4)
            throw std::runtime_error("Overlay bitmap exceeds its fixed extent");
        bytes_=canvas.pixels;
        const int error=int(submit(bytes_.data(),unsigned(canvas.width),unsigned(canvas.height)));
        if(error)throw std::runtime_error(std::string("Overlay image submission failed: ")+name+" code="+std::to_string(error));
        width_=canvas.width;height_=canvas.height;started_=now;pending_=true;++submissions_;
        return true;
    }
private:
    std::vector<std::uint8_t> bytes_;
    bool pending_=false,loaded_=false;
    double started_=0;
    double minimumInterval_=.1;
    int width_=0,height_=0;
    std::uint64_t submissions_=0,coalesced_=0;
};
} // namespace room_native
