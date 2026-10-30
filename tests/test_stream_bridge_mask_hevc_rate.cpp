#include "pc/stream-bridge-mask-hevc-rate.h"
#include "pc/stream-bridge-wire-budget.h"
#include "pc/stream-bridge-rgb-telemetry.h"
#include <cstdio>
#include <stdexcept>
#include <cstring>
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
void completedInputBudget(){
    MaskHevcRate rate;rate.initial(20000000,90,1,330000000);
    // A small mask already fits a low native budget. The configured VBR
    // ceiling must not itself be mistaken for20Mbps of transmitted traffic.
    for(unsigned n=0;n<4096;++n){
        rate.observe(1000,90,1,13000000);
        const auto p=rate.plan(20000000,90,1,n%2?330000000:13000000);
        check(!p.change,"fitting actual mask traffic cannot pump the encoder ceiling down and up");
        if(n>=MaskHevcRate::Window)check(p.cap==2600000,"completed-input QoS, not latest cold target, establishes the mask share");
    }
    check(rate.current().target==20000000,"unused mask quality ceiling remains available without sending filler bytes");
    // One captured low-budget picture and arbitrary cold observations cannot
    // relabel the16-picture history or infer a sustained capacity collapse.
    MaskHevcRate transient;transient.initial(20000000,90,1,330000000);
    for(unsigned n=0;n<MaskHevcRate::DecisionFrames;++n)transient.observe(15000,90,1,330000000);
    transient.observe(15000,90,1,13000000);
    for(unsigned n=0;n<4096;++n){const auto p=transient.plan(20000000,90,1,13000000);
        check(!p.change&&p.cap==20000000,"one low captured target and repeated cold ticks cannot cause an emergency cut");}
    // A real, sustained bandwidth collapse still protects RGB promptly.
    for(unsigned n=1;n<MaskHevcRate::Window;++n)transient.observe(15000,90,1,13000000);
    const auto cut=transient.plan(20000000,90,1,330000000);
    check(cut.change&&cut.cap==2600000&&cut.target<5000000,"sustained measured overshoot cuts the ceiling even if latest native QoS has bounced high");
    MaskHevcRate explicitLimit;explicitLimit.initial(20000000,90,1,330000000);
    for(unsigned n=0;n<MaskHevcRate::DecisionFrames;++n)explicitLimit.observe(1000,90,1,330000000);
    check(explicitLimit.plan(19000000,90,1,330000000).target==19000000,"a lower explicit user ceiling bypasses ordinary target hysteresis");
    check(explicitLimit.plan(1000000,90,1,330000000).target==1000000,"actual underfill never overrides an explicit lower user ceiling");
    MaskHevcRate unknown;unknown.initial(20000000,90,1,330000000);fill(unknown,MaskHevcRate::DecisionFrames,15000);
    const auto fallback=unknown.plan(20000000,90,1,13000000);
    check(fallback.change&&fallback.cap==2600000,"absent captured QoS conservatively retains latest known budget feedback");
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
    check(controlled.controlled(7)&&!controlled.controlled(8)&&!controlled.controlled(0),"only the current authority can select controlled buffer policy");
    controlled.observe(8,1000,2000,true);check(!controlled.reserve(7,90,1)&&controlled.reserve(8,90,1)==720000,"authority changes revoke old rate history");
    for(unsigned i=0;i<16;++i)controlled.observe(8,1000,21000,false);
    check(!controlled.controlled(8),"a sustained lossless route cannot retain a previous HEVC buffer policy");
}
void rgbTelemetry(){
    static_assert(sizeof(RgbOutputTelemetry)<4096,"diagnostics have a fixed small process-lifetime footprint");
    RgbOutputTelemetry t;RgbOutputSample s{};s.observedUs=200;s.encoderGeneration=7;s.submission=11;s.inputTimestamp=99;
    s.frameIndex=31;s.qp=20;s.rgbBytes=100;s.completeBytes=140;s.width=1152;s.height=4608;s.exactInput=true;
    s.qos.known=true;s.qos.revision=5;s.qos.frameRateNum=90;s.qos.frameRateDen=1;s.qos.requestedAverageBitRate=100000000;
    s.qos.appliedAverageBitRate=90000000;s.qos.rateControlMode=2;
    t.observe(s);check(!t.take(250).frames,"disabled diagnostics never observe native output");t.enable(true,100);t.observe(s);
    s.observedUs=300;s.submission=12;s.qp=50;s.rgbBytes=200;s.completeBytes=500;s.qos.appliedAverageBitRate=6000000;t.observe(s);
    s.observedUs=400;s.submission=13;s.qp=35;s.rgbBytes=900;s.completeBytes=950;s.qos.appliedAverageBitRate=50000000;t.observe(s);
    s.observedUs=500;s.submission=14;s.qp=30;s.rgbBytes=300;s.completeBytes=900;s.qos.appliedAverageBitRate=100000000;t.observe(s);
    s.observedUs=600;s.submission=0;s.qp=0;s.rgbBytes=s.completeBytes=20;s.exactInput=false;s.qos.appliedAverageBitRate=1;t.observe(s);
    check(!t.take(599).frames,"a backwards report clock cannot consume future observations");
    auto r=t.take(1000);
    check(r.frames==5&&r.unmatched==1&&r.rgbBytes==1520&&r.addedBytes==990&&r.maxRgbBytes==900&&r.maxAddedBytes==600,
        "RGB and complete-minus-original metadata remain separate over every accepted native output");
    check(r.qpSamples==5&&r.minQp==0&&r.maxQp==50&&r.qpSum==135,"raw valid QP zero is preserved without claiming visual quality");
    check(r.lowBudgetFrames==1&&r.highQpFrames==1&&r.lowBudgetHighQpFrames==1,"explicit low-budget/high-QP counts distinguish sustained pressure from a single worst sample");
    const auto& last=r.events[r.eventCount-1].sample;
    check(r.knownQos==4&&!last.exactInput&&!last.qos.known&&!last.qos.revision&&!last.qos.appliedAverageBitRate,
        "unmatched output cannot borrow a latest native configuration in diagnostic events");
    check(r.eventCount==4&&r.events[0].roles==(RgbOutputTelemetry::LowRgb|RgbOutputTelemetry::HighQp),
        "one transient low-budget high-QP picture survives later good frames without duplicate log rows");
    check(r.events[0].sample.submission==12&&r.events[0].sample.qos.appliedAverageBitRate==6000000,
        "worst-picture sample retains its own original input QoS after later changes");
    char text[RgbOutputLogBytes]{};check(formatRgbOutputSummary(text,sizeof text,r)&&std::strstr(text,"bytes rgb/added=1520/990"),"cold summary reports the observed original and added bytes");
    check(formatRgbOutputEvent(text,sizeof text,r,r.events[0])&&std::strstr(text,"age-us=700")&&std::strstr(text,"avg requested/passed=100000000/6000000"),
        "event age and exact captured budget are reconstructable from the cold log timestamp");
    check(!formatRgbOutputEvent(text,4,r,r.events[0]),"undersized diagnostic buffers report truncation");
    auto preserved=r.events[0];preserved.sample.qos.preservedVbvSpan=true;
    check(formatRgbOutputEvent(text,sizeof text,r,preserved)&&std::strstr(text,"flags=0x23"),
        "cold RGB event reports preserved native VBR span on its captured input only");
    check(!t.take(1100).frames,"cold interval reads cannot repeatedly count the same output");
    s.exactInput=true;s.qp=63;s.observedUs=1200;t.observe(s);
    s.observedUs=1199;t.observe(s);s.observedUs=1250;s.completeBytes=1;t.observe(s);
    s.completeBytes=RgbOutputTelemetry::MaximumBytes+1;t.observe(s);r=t.take(1300);
    check(r.frames==1&&r.rejected==3&&!r.qpSamples&&!r.minQp&&!r.maxQp,"invalid clocks/sizes and unsupported QP cannot pollute rate or QP aggregates");
    t.enable(false,0);t.enable(true,1400);s.observedUs=1500;s.completeBytes=30;s.qp=20;
    for(unsigned n=0;n<10000;++n){++s.observedUs;t.observe(s);}r=t.take(12000);
    check(r.frames==10000&&r.eventCount<=5,"continuous output cannot grow diagnostic storage or log-event count");
    auto huge=r;auto& e=huge.events[0];e.sample.encoderGeneration=e.sample.submission=e.sample.inputTimestamp=e.sample.qos.revision=UINT64_MAX;
    auto& q=e.sample.qos;q.frameRateNum=q.frameRateDen=q.rateControlMode=UINT32_MAX;
    q.requestedAverageBitRate=q.appliedAverageBitRate=q.requestedMaximumBitRate=q.appliedMaximumBitRate=UINT32_MAX;
    q.requestedVbvBufferSize=q.appliedVbvBufferSize=q.requestedVbvInitialDelay=q.appliedVbvInitialDelay=UINT32_MAX;
    q.reservedMetadataBitRate=q.meanMetadataBitRate=UINT32_MAX;
    check(formatRgbOutputEvent(text,sizeof text,huge,e),"all bounded scalar fields fit a complete1024-byte cold event line");
}
void nativeVbrSpan(){
    const auto current=WireBudget::boundVbv(100000000,32800000,120,1,1000000,800000);
    const auto native=WireBudget::boundVbv(100000000,32800000,120,1,1000000,800000,true);
    check(current.bits==273333&&native.bits==328000&&native.delay==262400,"controlled VBR shares the native buffer without a second one-frame cut");
    const auto observed=WireBudget::boundVbv(9667172,5417265,107,1,146290,0,true);
    check(observed.bits==81977&&observed.delay==0,"real QP50 trace restores proportional native headroom and automatic delay");
    for(const auto fps:{60u,90u,107u,132u,180u,240u}){
        const auto p=WireBudget::boundVbv(100000000,60000000,fps,1,3000000,0,true);
        check(p.bits==1800000&&!p.delay,"native FPS jitter cannot shrink explicit controlled-VBR buffer span");
        check(std::uint64_t(p.bits)+40000000/fps<=3000000,"RGB buffer plus one reserved metadata frame fits the original native burst budget");
    }
    const auto unknown=WireBudget::boundVbv(100000000,60000000,120,1,0,0,true);
    check(unknown.bits==500000&&unknown.delay==500000,"unknown native buffer retains the existing conservative one-picture fallback");
    const auto small=WireBudget::boundVbv(100000000,60000000,120,1,6000,6200,true);
    check(small.bits==3600&&small.delay==3600,"explicit small native buffers and bounded delay remain authoritative");
    std::uint64_t seed=9;
    for(unsigned i=0;i<10000;++i){seed=seed*6364136223846793005ull+1;
        const auto total=std::max(1u,std::uint32_t(seed>>32)),video=1u+std::uint32_t(seed%total),buffer=1u+std::uint32_t((seed>>16)%UINT32_MAX);
        const auto p=WireBudget::boundVbv(total,video,90,1,buffer,buffer,true);
        check(p.bits<=buffer&&p.delay<=p.bits,"proportional sharing cannot enlarge caller buffer or delay");
        check(std::uint64_t(p.bits)*total<=std::uint64_t(buffer)*video+total,"declared RGB VBV duration is no larger than native duration apart from one-bit rounding");
    }
    check(!WireBudget::boundVbv(100,101,90,1,10,0,true).bits,"invalid rate increase remains refused");
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
int main(){try{controller();jitterFeedback();completedInputBudget();budgets();rgbTelemetry();nativeVbrSpan();replay();std::printf("%u mask-rate/shared-budget checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
