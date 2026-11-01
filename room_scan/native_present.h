// GPU copies complete before publishing UI state. Stable images are retained;
// unchanged menus and rate-limited animation frames need no new submission.
#pragma once
#include "native_raster.h"
#include <nlohmann/json.hpp>
#include <string>

namespace room_native {
inline bool sameControls(const nlohmann::json& displayed,const nlohmann::json& current){
    return displayed.at("page")==current.at("page")&&displayed.at("buttons")==current.at("buttons");
}
class TextureOverlayImage {
public:
    explicit TextureOverlayImage(double minimumInterval=.1):minimumInterval_(minimumInterval){}
    bool loaded()const{return submissions_!=0;}
    std::uint64_t submissions()const{return submissions_;}
    std::uint64_t coalesced()const{return coalesced_;}
    bool matches(const Canvas& image)const{return loaded()&&image.width==width_&&image.height==height_&&bytes_==image.pixels;}
    template<class Submit>bool update(const Canvas& canvas,double now,const char* name,Submit submit){
        if(loaded()&&(canvas.width!=width_||canvas.height!=height_))throw std::runtime_error("Overlay texture extent changed");
        if(matches(canvas))return false;
        if(loaded()&&now-started_<minimumInterval_){++coalesced_;return false;}
        if(canvas.width<=0||canvas.height<=0||canvas.width>1024||canvas.height>1024
            ||canvas.pixels.size()!=std::size_t(canvas.width)*canvas.height*4)
            throw std::runtime_error("Overlay texture exceeds its fixed extent");
        const int error=int(submit(canvas));
        if(error)throw std::runtime_error(std::string("Overlay texture submission failed: ")+name+" code="+std::to_string(error));
        bytes_=canvas.pixels;width_=canvas.width;height_=canvas.height;started_=now;++submissions_;
        return true;
    }
private:
    std::vector<std::uint8_t> bytes_;
    double started_=0,minimumInterval_=.1;
    int width_=0,height_=0;
    std::uint64_t submissions_=0,coalesced_=0;
};
} // namespace room_native
