#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace stream_bridge::sender {
// Rate estimates only. No frame/source permission, clocks, allocation or I/O.
// The user preference is a ceiling; allocate at most one fifth of the current
// original stream target to masks (with estimator headroom kept separately).
// Actual authenticated wire bytes close the loop when a hardware bitrate target overshoots (all-intra CBR did so in logs).
class MaskHevcRate {
public:
    static constexpr unsigned Window=16;
    static constexpr std::uint32_t Minimum=100000,Maximum=200000000;
    struct Plan {std::uint32_t target=0,cap=0,num=0,den=0,stream=0;bool change=false;};
    static std::uint32_t budget(std::uint32_t user,std::uint32_t stream)noexcept{
        user=std::clamp(user,Minimum,Maximum);return std::max(Minimum,std::min(user,stream?std::max(Minimum,stream/5):user));
    }
    Plan initial(std::uint32_t user,std::uint32_t num,std::uint32_t den,std::uint32_t stream)noexcept{
        const Plan p{budget(user,stream),budget(user,stream),num,den,stream,true};commit(p);return p;
    }
    void observe(std::size_t bytes)noexcept{
        if(!bytes||bytes>4u*1024u*1024u)return;
        if(count_==Window)sum_-=samples_[at_];else ++count_;
        samples_[at_]=static_cast<std::uint32_t>(bytes);sum_+=bytes;at_=(at_+1)%Window;
    }
    std::uint32_t observed(std::uint32_t num,std::uint32_t den)const noexcept{
        if(!count_||!num||!den)return 0;
        const auto divisor=std::uint64_t(count_)*den,bits=sum_*8*num;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>((bits+divisor-1)/divisor,UINT32_MAX));
    }
    Plan plan(std::uint32_t user,std::uint32_t num,std::uint32_t den,std::uint32_t stream)const noexcept{
        if(!num||!den||!current_.target)return {};
        Plan p=current_;p.cap=budget(user,stream);p.stream=stream;
        const auto left=std::uint64_t(num)*current_.den,right=std::uint64_t(current_.num)*den;
        // Do not reconfigure on the native sender's per-frame FPS jitter.
        if((left>right?left-right:right-left)>right/10){p.num=num;p.den=den;}
        auto target=std::min(current_.target,p.cap);
        const auto actual=observed(num,den);
        if(count_==Window&&actual>std::uint64_t(p.cap)*105/100){
            target=std::min<std::uint64_t>(target,std::uint64_t(current_.target)*p.cap*9/(std::uint64_t(actual)*10));
        }else if((!count_&&p.cap>current_.cap)||(count_==Window&&actual<std::uint64_t(p.cap)*4/5)){
            target=std::min<std::uint64_t>(p.cap,std::uint64_t(target)*9/8+1);
        }
        target=std::max(Minimum,target);
        const auto delta=target>current_.target?target-current_.target:current_.target-target;
        p.change=p.num!=current_.num||p.den!=current_.den||current_.target>p.cap||delta>=std::max(1u,current_.target/8);
        p.target=p.change?target:current_.target;return p;
    }
    void commit(const Plan& p)noexcept{current_=p;current_.change=false;samples_={};count_=at_=0;sum_=0;}
    Plan current()const noexcept{return current_;}
private:
    Plan current_{};std::array<std::uint32_t,Window> samples_{};unsigned count_=0,at_=0;std::uint64_t sum_=0;
};
} // namespace stream_bridge::sender
