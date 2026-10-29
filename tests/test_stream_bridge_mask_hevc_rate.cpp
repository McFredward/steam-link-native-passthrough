#include "pc/stream-bridge-mask-hevc-rate.h"
#include "pc/stream-bridge-wire-budget.h"
#include <cstdio>
#include <stdexcept>
using namespace stream_bridge::sender;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void fill(MaskHevcRate& rate,unsigned count,std::size_t bytes,unsigned fps=90){for(unsigned n=0;n<count;++n)rate.observe(bytes,fps,1);}
void controller(){
    check(MaskHevcRate::budget(20000000,0)==20000000,"unknown stream budget retains explicit user ceiling");
    check(MaskHevcRate::budget(20000000,330000000)==20000000,"ample link retains explicit mask preference");
    check(MaskHevcRate::budget(20000000,13000000)==2600000,"13Mbps link grants mask20percent");
    MaskHevcRate rate;rate.initial(20000000,90,1,330000000);
    for(unsigned i=0;i<4096;++i)check(!rate.plan(20000000,i%2?60:120,1,13000000+i*1000).change,
        "arbitrarily many cold ticks without outputs cannot reset the encoder or its byte-feedback window");
    fill(rate,MaskHevcRate::DecisionFrames,61000);
    auto p=rate.plan(20000000,140,1,330000000);
    check(p.change&&p.target<9000000&&p.target>7000000&&p.num==90,"measured44Mbps corrects the20Mbps target without following latest FPS jitter");rate.commit(p);
    check(!rate.observed(90,1),"new applied epoch never reuses old-rate byte samples");
    fill(rate,MaskHevcRate::Window-1,30000);
    check(!rate.plan(20000000,90,1,13000000).change,"even an urgent budget cut waits for actual new outputs");
    rate.observe(30000,90,1);p=rate.plan(20000000,90,1,13000000);
    check(p.change&&p.target<=2600000,"major stream-budget collapse uses the shorter completed-output gate");rate.commit(p);
    fill(rate,MaskHevcRate::DecisionFrames,8000);p=rate.plan(20000000,90,1,13000000);
    check(p.change&&p.target<1300000,"actual overshoot remains corrected within a small shared budget");rate.commit(p);
    const auto stable=p.target;fill(rate,MaskHevcRate::DecisionFrames,(std::uint64_t(stable)*22/10+719)/720);
    p=rate.plan(20000000,120,1,13000000);
    check(!p.change&&p.num==90,"latest native FPS alone cannot override the median completed-output clock");
    fill(rate,MaskHevcRate::Window,2000,120);p=rate.plan(20000000,90,1,13000000);
    check(p.change&&p.num==120&&p.den==1,"sustained120Hz observations update nominal private clock");rate.commit(p);
    const auto before=rate.current().target;fill(rate,MaskHevcRate::GrowthFrames-1,1024,120);
    check(!rate.plan(20000000,120,1,330000000).change,"transient easy masks cannot repeatedly pump quality up and clear feedback");
    rate.observe(1024,120,1);p=rate.plan(20000000,120,1,330000000);
    check(p.change&&p.target>before&&p.target<=std::uint64_t(before)*5/4+1,"quality recovery requires a longer stable measured interval");
}
void jitterFeedback(){
    MaskHevcRate rate;rate.initial(20000000,90,1,110000000);unsigned changes=0;
    const unsigned jitter[]{68,112,89,93};
    for(unsigned tick=0;tick<1600;++tick){
        const auto p=rate.plan(20000000,jitter[tick%4],1,110000000+tick%3*1000000);
        if(p.change){rate.commit(p);++changes;}
        if(tick%4!=0){const auto actual=std::uint64_t(rate.current().target)*22/10;rate.observe((actual+719)/720,jitter[tick%4],1);}
    }
    check(changes<=8,"mixed worker/output FPS jitter cannot cause thousands of private reconfigurations");
    check(rate.current().target<11000000&&rate.observed(90,1)<=22000000,
        "completed-byte feedback makes progress under continuous native QoS jitter");
    MaskHevcRate stalled;stalled.initial(20000000,90,1,100000000);
    for(unsigned i=0;i<MaskHevcRate::DecisionFrames;++i)stalled.observe(44000,90,1);
    const auto blocked=stalled.plan(20000000,90,1,100000000);check(blocked.change,"unresponsive encoder still creates a real rate correction");
    for(unsigned i=0;i<100;++i)check(stalled.plan(20000000,90,1,100000000).target==blocked.target,
        "uncommitted/refused API changes cannot accumulate further target reductions");
}
void budgets(){
    WireBudget legacy,controlled;
    for(unsigned i=0;i<128;++i){legacy.observe(7,1000,21000);controlled.observe(7,1000,21000,true);}
    legacy.observe(7,1000,251000);controlled.observe(7,1000,251000,false);
    check(legacy.reserve(7,90,1)==180000000,"lossless historical peak accounting remains unchanged");
    check(controlled.peakRate(7,90,1)==180000000,"actual fallback peak is still reported in controlled mode");
    check(controlled.planningMean(7,90,1)>14000000&&controlled.reserve(7,90,1)<40000000,"fallback bytes are included without pretending every following frame repeats the spike");
    for(unsigned i=0;i<16;++i)controlled.observe(7,1000,21000,true);
    check(controlled.reserve(7,90,1)==18000000&&controlled.peakRate(7,90,1)==180000000,"recent controlled rate recovers while the full old peak remains visible");
    controlled.observe(8,1000,2000,true);check(!controlled.reserve(7,90,1)&&controlled.reserve(8,90,1)==720000,"authority changes revoke old rate history");
}
void replay(){
    // Deterministic software plant: every NVENC target overshoots2.2x. This is
    // evidence about control logic, not a claim about measured GPU behaviour.
    MaskHevcRate rate;rate.initial(20000000,90,1,330000000);WireBudget wire;
    std::uint32_t video=0,actual=0;unsigned floors=0;
    for(unsigned frame=0;frame<2560;++frame){
        const std::uint32_t stream=frame<320?330000000:(frame<1280?13000000:330000000);
        const auto plan=rate.plan(20000000,90,1,stream);if(plan.change)rate.commit(plan);
        actual=rate.current().target*22/10;
        const auto bytes=(std::uint64_t(actual)+719)/720;
        rate.observe(bytes);wire.observe(42,50000,50000+bytes,true);
        video=WireBudget::videoRate(stream,wire.reserve(42,90,1));
        if(frame>=640&&frame<1280&&video<=1000000)++floors;
        if(frame==1279)check(actual<2600000&&video>=9000000,"observed13Mbps/44Mbps failure converges to a useful RGB share even with encoder overshoot");
    }
    check(!floors,"stable low-budget scene cannot repeatedly starve RGB to1Mbps");
    check(video>300000000,"a recovered link gives its capacity back to colour video");
}
}
int main(){try{controller();jitterFeedback();budgets();replay();std::printf("%u mask-rate/shared-budget checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
