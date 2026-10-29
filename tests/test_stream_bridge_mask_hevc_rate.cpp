#include "pc/stream-bridge-mask-hevc-rate.h"
#include "pc/stream-bridge-wire-budget.h"
#include <cstdio>
#include <stdexcept>
using namespace stream_bridge::sender;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
void controller(){
    check(MaskHevcRate::budget(20000000,0)==20000000,"unknown stream budget retains explicit user ceiling");
    check(MaskHevcRate::budget(20000000,330000000)==20000000,"ample link does not invent a lower mask quality setting");
    check(MaskHevcRate::budget(20000000,13000000)==2600000,"13Mbps link grants mask20percent instead of starving colour");
    MaskHevcRate rate;auto applied=rate.initial(20000000,90,1,330000000);
    for(unsigned n=0;n<16;++n)rate.observe(61000); //43.92Mbps, matching the observed all-intra overshoot.
    auto p=rate.plan(20000000,90,1,330000000);
    check(p.change&&p.target<9000000&&p.target>7000000,"20Mbps target producing44Mbps is corrected from actual bytes");rate.commit(p);
    check(!rate.observed(90,1),"new rate epoch never reuses old high-rate samples");
    p=rate.plan(20000000,90,1,13000000);
    check(p.change&&p.target==2600000,"sudden original stream-budget reduction applies immediately");rate.commit(p);
    for(unsigned n=0;n<16;++n)rate.observe(8000); //5.76Mbps from2.6Mbps target.
    p=rate.plan(20000000,90,1,13000000);check(p.change&&p.target<1300000,"overshoot feedback still works inside a small shared budget");rate.commit(p);
    const auto stable=p.target;
    for(unsigned n=0;n<16;++n)rate.observe((std::uint64_t(stable)*22/10+719)/720);
    p=rate.plan(20000000,91,1,13000000);check(!p.change&&p.num==90,"small native FPS jitter and compliant observed rate do not churn RC");
    p=rate.plan(20000000,120,1,13000000);check(p.change&&p.num==120,"real nominal FPS change updates bitrate-per-frame conversion");
    rate.commit(p);const auto beforeGrowth=rate.current().target;for(unsigned n=0;n<16;++n)rate.observe(1024);
    p=rate.plan(20000000,120,1,330000000);check(p.change&&p.target>beforeGrowth&&p.target<=std::uint64_t(beforeGrowth)*9/8+1,"quality recovers gradually after pressure instead of latching a low target");
    (void)applied;
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
    for(unsigned frame=0;frame<640;++frame){
        const std::uint32_t stream=frame<160?330000000:(frame<480?13000000:330000000);
        const auto plan=rate.plan(20000000,90,1,stream);if(plan.change)rate.commit(plan);
        actual=rate.current().target*22/10;
        const auto bytes=(std::uint64_t(actual)+719)/720;
        rate.observe(bytes);wire.observe(42,50000,50000+bytes,true);
        video=WireBudget::videoRate(stream,wire.reserve(42,90,1));
        if(frame>=240&&frame<480&&video<=1000000)++floors;
        if(frame==479)check(actual<2600000&&video>=9000000,"observed13Mbps/44Mbps failure converges to a useful RGB share even with encoder overshoot");
    }
    check(!floors,"stable low-budget scene cannot repeatedly starve RGB to1Mbps");
    check(video>300000000,"a recovered link gives its capacity back to colour video");
}
}
int main(){try{controller();budgets();replay();std::printf("%u mask-rate/shared-budget checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
