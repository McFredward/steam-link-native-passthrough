#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace stream_bridge_renderer {
// Cold-worker diagnostics only. Keep initial milestone reports bounded, then
// report active progress at most every five seconds for the process lifetime.
class ReportGate {
public:
    static constexpr std::uint64_t PeriodMicros=5000000;
    bool due(std::uint64_t now,std::uint64_t progress,std::uint64_t milestones)noexcept{
        if(!milestones)return false;
        unsigned level=0;for(auto value=progress;value>1;value>>=1)++level;
        const bool initial=initialLines_<48&&((milestones&~milestones_)||level>level_);
        const bool periodic=now>=next_&&progress!=progress_;
        if(!initial&&!periodic)return false;
        if(initial)++initialLines_;
        ++reports_;
        milestones_|=milestones;level_=std::max(level_,level);progress_=progress;
        next_=now>std::numeric_limits<std::uint64_t>::max()-PeriodMicros?std::numeric_limits<std::uint64_t>::max():now+PeriodMicros;
        return true;
    }
    std::uint64_t reports()const noexcept{return reports_;}
private:
    std::uint64_t milestones_=0,progress_=0,next_=0,reports_=0;unsigned level_=0,initialLines_=0;
};
}
