// Select both eyes of one hardware exposure before advancing the copy budget.
// This is a work scheduler, never proof that two images form a valid pair.
#pragma once
#include <cstdint>

namespace room_camera_api {
class StereoCopySchedule {
    static constexpr std::uint64_t interval=100000000u,skew=100000u;
    std::uint64_t selected_=0;
    unsigned copied_=0;
    bool same(std::uint64_t sof)const{
        return (sof>selected_?sof-selected_:selected_-sof)<=skew;
    }
public:
    bool wants(unsigned eye,std::uint64_t sof)const{
        if(eye>1||!sof)return false;
        if(!selected_)return true;
        if(same(sof))return !(copied_&(1u<<eye));
        // The tolerance admits a stereo mate on either side of the due time.
        // No absolute time bin independently selects a different eye exposure.
        return sof>selected_&&sof-selected_>=interval-skew;
    }
    bool copied(unsigned eye,std::uint64_t sof){
        if(!wants(eye,sof))return false;
        if(!selected_||!same(sof)){selected_=sof;copied_=0;}
        copied_|=1u<<eye;
        return true;
    }
};
} // namespace room_camera_api
