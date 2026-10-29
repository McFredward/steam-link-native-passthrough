#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace stream_bridge::sender {
// Rate policy only, never frame/image permission. Account for bytes added AFTER
// NVENC so a producer's video budget also has room for its lossless metadata.
// Fixed storage, no clocks, allocation, I/O or native encoder calls.
class WireBudget {
public:
    static constexpr std::size_t Window=128;
    struct Vbv {std::uint32_t bits=0,delay=0;};
    void reset() noexcept { authority_=0;sum_=0;count_=at_=0;samples_={};controlled_={}; }
    void observe(std::uint64_t authority,std::size_t original,std::size_t complete,bool controlled=false) noexcept {
        if(authority!=authority_){reset();authority_=authority;}
        if(!authority||!original||complete<original||complete>64*1024*1024)return;
        if(count_==samples_.size())sum_-=samples_[at_];else ++count_;
        controlled_[at_]=controlled;
        samples_[at_]=static_cast<std::uint32_t>(complete-original);sum_+=samples_[at_];at_=(at_+1)%samples_.size();
    }
    std::uint32_t reserve(std::uint64_t authority,std::uint32_t fpsNum,std::uint32_t fpsDen)const noexcept {
        if(!valid(authority,fpsNum,fpsDen))return 0;
        const auto peak=peakRate(authority,fpsNum,fpsDen);
        if(controlledWindow()){
            // Recent actual bytes include fallback spikes. Rate-controlled
            // traffic must not reserve a single old spike for128 more frames.
            // The full peak remains observable; this is an estimate, not a cap.
            const auto mean=planningMean(authority,fpsNum,fpsDen);
            return std::min(peak,static_cast<std::uint32_t>(std::min<std::uint64_t>((std::uint64_t(mean)*5+3)/4,UINT32_MAX)));
        }
        // Attack on the first observed frame; retain a peak for 128 outputs.
        // This is a rate estimate only, never another frame's mask permission.
        return peak;
    }
    std::uint32_t peakRate(std::uint64_t authority,std::uint32_t fpsNum,std::uint32_t fpsDen)const noexcept{
        if(!valid(authority,fpsNum,fpsDen))return 0;
        std::uint32_t peak=0;for(std::size_t i=0;i<count_;++i)peak=std::max(peak,samples_[i]);
        return rate(std::uint64_t(peak)*count_,fpsNum,fpsDen);
    }
    std::uint32_t average(std::uint64_t authority,std::uint32_t fpsNum,std::uint32_t fpsDen)const noexcept {
        return valid(authority,fpsNum,fpsDen)?rate(sum_,fpsNum,fpsDen):0;
    }
    std::uint32_t planningMean(std::uint64_t authority,std::uint32_t fpsNum,std::uint32_t fpsDen)const noexcept{
        if(!valid(authority,fpsNum,fpsDen))return 0;
        if(!controlledWindow())return average(authority,fpsNum,fpsDen);
        const auto recent=std::min<std::size_t>(count_,16);std::uint64_t sum=0;
        for(std::size_t n=0;n<recent;++n)sum+=samples_[(at_+Window-1-n)%Window];
        const auto divisor=std::uint64_t(recent)*fpsDen,bits=sum*8*fpsNum;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>((bits+divisor-1)/divisor,UINT32_MAX));
    }
    static Vbv boundVbv(std::uint32_t requested,std::uint32_t video,std::uint32_t fpsNum,std::uint32_t fpsDen,
        std::uint32_t buffer,std::uint32_t delay)noexcept {
        if(!requested||!video||video>requested||!fpsNum||!fpsDen||fpsNum>1000000||fpsDen>1000000||fpsNum>std::uint64_t(fpsDen)*1000)return {};
        const auto perFrame=std::max<std::uint64_t>(1,std::uint64_t(video)*fpsDen/fpsNum);
        const auto scaled=buffer?std::max<std::uint64_t>(1,std::uint64_t(buffer)*video/requested):perFrame;
        const auto bits=static_cast<std::uint32_t>(std::min<std::uint64_t>(std::min(perFrame,scaled),UINT32_MAX));
        const auto initial=delay?std::max<std::uint64_t>(1,std::uint64_t(delay)*video/requested):bits;
        return {bits,static_cast<std::uint32_t>(std::min<std::uint64_t>(initial,bits))};
    }
    static std::uint32_t videoRate(std::uint32_t requested,std::uint32_t metadata)noexcept {
        // A proportional 25% quality floor overfills the actual wire budget
        // when coverage takes most of it (e.g. 297 Mb/s total, 273 Mb/s mask).
        // Keep a small encoder floor instead, leaving every other available
        // bit for video. Extremely small caller budgets retain the old floor.
        // Metadata that alone exceeds the target is still reported as pressure;
        // never discard exact coverage or claim an impossible total fits.
        const auto floor=std::min<std::uint32_t>(1000000,requested/4+(requested%4!=0));
        return requested-std::min(metadata,requested-floor);
    }
private:
    bool controlledWindow()const noexcept{
        const auto recent=std::min<std::size_t>(count_,16);if(!recent)return false;
        unsigned controlled=0;for(std::size_t n=0;n<recent;++n)controlled+=controlled_[(at_+Window-1-n)%Window];
        return controlled&&controlled>=recent-recent/8;
    }
    std::uint64_t authority_=0,sum_=0;
    bool valid(std::uint64_t authority,std::uint32_t num,std::uint32_t den)const noexcept {
        return authority&&authority==authority_&&count_&&den&&num&&num<=1000000&&den<=1000000&&num<=std::uint64_t(den)*1000;
    }
    std::uint32_t rate(std::uint64_t sum,std::uint32_t num,std::uint32_t den)const noexcept {
        const auto divisor=std::uint64_t(count_)*den,bits=sum*8*num;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>((bits+divisor-1)/divisor,UINT32_MAX));
    }
    std::array<std::uint32_t,Window> samples_{};
    std::array<bool,Window> controlled_{};
    std::size_t count_=0,at_=0;
};
}
