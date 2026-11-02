// Bounded metadata-only diagnostics for acquisition latency and refusal gates.
#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>

namespace room_native {
struct CaptureStats {
    enum Result {NoPair,Timing,PoseHistory,Motion,ClockBudget,NoDepth,Accepted,Failure,ResultCount};
    enum Stage {PairRead,PoseRead,Rectify,Stereo,Triangulate,StageCount};
    struct Cost {double sum=0,maximum=0;unsigned count=0;void add(double ms){sum+=ms;maximum=std::max(maximum,ms);++count;}};
    std::array<unsigned,ResultCount> outcomes{};std::array<Cost,StageCount> costs{};
    double lastReport=0;unsigned reports=0;double maximumAgeMs=0;std::uint64_t maxQueryNs=0;
    static double now(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    struct Attempt {
        CaptureStats& owner;Result result=Failure;
        ~Attempt(){++owner.outcomes[result];owner.report();}
    };
    struct Measure {
        Cost& cost;double started=now();~Measure(){cost.add((now()-started)*1000);}
    };
    void report(){const double time=now();if(time-lastReport<5||reports>=120)return;lastReport=time;++reports;
        std::fprintf(stderr,"Room capture gates: accepted=%u no_pair=%u exposure=%u pose=%u motion=%u clock=%u no_depth=%u failure=%u max_age_ms=%.2f max_query_us=%llu\n",
            outcomes[Accepted],outcomes[NoPair],outcomes[Timing],outcomes[PoseHistory],outcomes[Motion],outcomes[ClockBudget],outcomes[NoDepth],outcomes[Failure],maximumAgeMs,
            static_cast<unsigned long long>(maxQueryNs/1000));
        std::fprintf(stderr,"Room capture cost: read_ms=%.2f pose_ms=%.2f rectify_ms=%.2f stereo_ms=%.2f triangulate_ms=%.2f\n",
            average(PairRead),average(PoseRead),average(Rectify),average(Stereo),average(Triangulate));
    }
    double average(Stage stage)const{const auto& value=costs[stage];return value.count?value.sum/value.count:0;}
};
} // namespace room_native
