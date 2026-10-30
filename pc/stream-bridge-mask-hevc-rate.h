#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <numeric>

namespace stream_bridge::sender {
// Rate estimates only. No frame/source permission, clocks, allocation or I/O.
// The user preference is a ceiling; allocate at most one fifth of the current
// original stream target to actual mask bytes (headroom kept separately).
// Actual authenticated wire bytes close the loop when a hardware target
// overshoots. Decisions use completed pictures, never worker-tick frequency.
class MaskHevcRate {
public:
    static constexpr unsigned Window=16,DecisionFrames=64,GrowthFrames=128;
    static constexpr std::uint32_t Minimum=100000,Maximum=200000000;
    struct Plan {std::uint32_t target=0,cap=0,num=0,den=0,stream=0;bool change=false;};
    static std::uint32_t budget(std::uint32_t user,std::uint32_t stream)noexcept{
        user=std::clamp(user,Minimum,Maximum);return std::max(Minimum,std::min(user,stream?std::max(Minimum,stream/5):user));
    }
    Plan initial(std::uint32_t user,std::uint32_t num,std::uint32_t den,std::uint32_t stream)noexcept{
        const Plan p{budget(user,stream),budget(user,stream),num,den,stream,true};commit(p);return p;
    }
    void observe(std::size_t bytes,std::uint32_t num=0,std::uint32_t den=0,std::uint32_t stream=0)noexcept{
        if(!bytes||bytes>4u*1024u*1024u)return;
        if(!num||!den){num=current_.num;den=current_.den;}
        if(!num||!den||num>1000000||den>1000000||num<std::uint64_t(den)||num>std::uint64_t(den)*1000)return;
        if(count_==Window)sum_-=samples_[at_];else ++count_;
        samples_[at_]=static_cast<std::uint32_t>(bytes);sum_+=bytes;
        fps_[at_]=static_cast<std::uint32_t>(std::uint64_t(num)*1000/den);
        streams_[at_]=stream; // Original QoS captured with this encoded input, not a cold worker tick.
        at_=(at_+1)%Window;if(observations_!=UINT32_MAX)++observations_;
    }
    std::uint32_t observed(std::uint32_t num,std::uint32_t den)const noexcept{
        if(!count_||!num||!den)return 0;
        const auto divisor=std::uint64_t(count_)*den,bits=sum_*8*num;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>((bits+divisor-1)/divisor,UINT32_MAX));
    }
    Plan plan(std::uint32_t user,std::uint32_t num,std::uint32_t den,std::uint32_t stream)const noexcept{
        if(!num||!den||!current_.target)return {};
        Plan p=current_;p.cap=budget(user,stream);p.stream=stream;
        // Native encoder targets can fluctuate at individual-picture
        // cadence. Only completed-input QoS can establish a sustained mask
        // share. Unknown entries do not manufacture link capacity.
        std::array<std::uint32_t,Window> known{};unsigned knownCount=0;
        for(unsigned i=0;i<count_;++i)if(streams_[i])known[knownCount++]=streams_[i];
        if(knownCount>=Window/2){
            std::sort(known.begin(),known.begin()+knownCount);
            const auto median=static_cast<std::uint32_t>((std::uint64_t(known[(knownCount-1)/2])+known[knownCount/2]+1)/2);
            p.cap=budget(user,median);
        }
        // Native QoS contains instantaneous frame-rate and target jitter.
        // Cold ticks are not samples: never erase feedback before enough actual
        // encoded outputs arrive. The configured VBR ceiling is not traffic:
        // cutting a ceiling whose actual bytes already fit just creates a
        // repeated cut/grow cycle. Actual overshoot has the shorter gate.
        if(count_<Window)return unchanged(p);
        auto sorted=fps_;std::sort(sorted.begin(),sorted.end());
        const auto milli=(sorted[Window/2-1]+sorted[Window/2]+1)/2;
        const auto divisor=std::gcd(milli,1000u),medianNum=milli/divisor,medianDen=1000u/divisor;
        const auto left=std::uint64_t(medianNum)*current_.den,right=std::uint64_t(current_.num)*medianDen;
        if((left>right?left-right:right-left)>right/10){p.num=medianNum;p.den=medianDen;}
        const auto actual=observed(medianNum,medianDen);
        const bool emergency=std::uint64_t(actual)>std::uint64_t(p.cap)*2;
        if(observations_<(emergency?Window:DecisionFrames))return unchanged(p);
        auto target=std::min(current_.target,std::clamp(user,Minimum,Maximum));
        if(actual>std::uint64_t(p.cap)*11/10){
            target=static_cast<std::uint32_t>(std::min<std::uint64_t>(target,std::uint64_t(current_.target)*p.cap*9/(std::uint64_t(actual)*10)));
        }else if(observations_>=GrowthFrames&&target<p.cap&&actual<std::uint64_t(p.cap)*4/5){
            target=static_cast<std::uint32_t>(std::min<std::uint64_t>(p.cap,std::uint64_t(target)*5/4+1));
        }
        target=std::max(Minimum,target);
        const auto delta=target>current_.target?target-current_.target:current_.target-target;
        p.change=p.num!=current_.num||p.den!=current_.den||current_.target>std::clamp(user,Minimum,Maximum)||delta>=std::max(1u,current_.target/8);
        p.target=p.change?target:current_.target;return p;
    }
    void commit(const Plan& p)noexcept{current_=p;current_.change=false;samples_={};fps_={};streams_={};count_=at_=observations_=0;sum_=0;}
    unsigned samplesSinceChange()const noexcept{return observations_;}
    Plan current()const noexcept{return current_;}
private:
    static Plan unchanged(Plan p)noexcept{p.change=false;return p;}
    Plan current_{};std::array<std::uint32_t,Window> samples_{},fps_{},streams_{};unsigned count_=0,at_=0,observations_=0;std::uint64_t sum_=0;
};
} // namespace stream_bridge::sender
