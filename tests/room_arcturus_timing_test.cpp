#include "room_scan/native_arcturus_timing.h"
#include <cstdio>
using namespace room_native;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
Json fixture(){return {{"encoding","nv12-u8-luma-candidate"},{"width",616},{"height",616},{"source_pitch_bytes",3328},{"source_height",2464},
    {"queue_mapping_exact",true},{"copy_stable",true},{"kernel_role_verified",true},
    {"capture_metadata",{{"capture_clock","CLOCK_MONOTONIC_RAW"},{"v4l_timestamp_matches",true},
        {"sof_raw_ns",1000000000ULL},{"eof_raw_ns",1010827500ULL},{"previous_sof_raw_ns",983331875ULL}}},
    {"observed_exposure_registers",{{"successful_i2c_result",true},{"unchanged_through_eof",true},
        {"same_value_since_raw_ns",900000000ULL},{"group_release_raw_ns",990000000ULL},
        {"same_value_group_count",6},{"coarse_integration_lines",1194}}}};}
}
int main(){try{
    auto frame=fixture();const auto t=arcturusTiming(frame);
    check(t&&t->integration==5205840ULL,"actual coarse integration units");
    // Last active row finishes one row before EOF in this native timing model;
    // its integration center is half an exposure earlier.
    check(t->lastRowCenter==1010827500ULL-4360-2602920ULL,"EOF-anchored last-row center");
    check(t->atRow(0)==t->firstRowCenter&&t->atRow(2463)==t->lastRowCenter,"raw rows retain their distinct exposure times");
    check(t->center<t->lastRowCenter&&t->center>t->firstRowCenter,"center never substitutes the dequeue timestamp");
    auto filtered=frame;filtered["encoding"]="nv12-u8-area4-candidate";
    filtered["sampling"]={{"step",4},{"area",{4,4}},{"center_offset",{1.5,1.5}}};
    check(arcturusSampleOffset(filtered)==1.5f&&arcturusTiming(filtered)->firstRowCenter==t->firstRowCenter,"area sampling retains physical row timing with its distinct pixel center");
    filtered["sampling"]["center_offset"]={0,0};
    check(!arcturusSampleOffset(filtered)&&!arcturusTiming(filtered),"wrong area center cannot reuse point-sampling geometry");
    filtered=frame;filtered["sampling"]={{"step",4},{"area",{4,4}},{"center_offset",{1.5,1.5}}};
    check(!arcturusTiming(filtered),"point format cannot claim averaged pixels");
    for(unsigned tile=0;tile<4;++tile){
        filtered=fixture();filtered["encoding"]="nv12-u8-area2-tile-candidate";
        filtered["sampling"]={{"step",2},{"area",{2,2}},{"center_offset",{.5,.5}}};
        filtered["tile"]={{"index",tile},{"origin",{(tile%2)*616,(tile/2)*616}},{"full_extent",{1232,1232}}};
        check(arcturusSampleOffset(filtered)==.5f&&arcturusTiming(filtered)->firstRowCenter==t->firstRowCenter,"every high-resolution tile retains the same native exposure and explicit center");
        filtered["tile"]["origin"]={0,1};check(!arcturusTiming(filtered),"unverified tile coordinates cannot reuse exposure proof");
    }
    frame["observed_exposure_registers"]["same_value_since_raw_ns"]=990000000ULL;
    check(!arcturusTiming(frame),"newly changed setting cannot attest current capture");
    frame=fixture();frame["observed_exposure_registers"]["unchanged_through_eof"]=false;
    check(!arcturusTiming(frame),"a setting change during readout is refused");
    frame=fixture();frame["capture_metadata"]["eof_raw_ns"]=1005000000ULL;
    check(!arcturusTiming(frame),"another sensor readout mode is refused");
    frame=fixture();frame["observed_exposure_registers"]["coarse_integration_lines"]=10000;
    check(!arcturusTiming(frame),"long-exposure mode is outside this capture contract");
    frame=fixture();auto& e=frame["observed_exposure_registers"];
    e["unchanged_through_eof"]=false;e["envelope_verified"]=true;e["envelope_anchor_raw_ns"]=950000000ULL;
    e["envelope_min_coarse"]=1194;e["envelope_max_coarse"]=1200;
    const auto bounded=arcturusTiming(frame);
    check(bounded&&bounded->uncertainty==6541&&bounded->minimumIntegration==1194*4360ULL
        &&bounded->maximumIntegration==1200*4360ULL,"verified small exposure changes retain a conservative time interval");
    check(std::abs(double(t->center)-double(bounded->center))<=double(bounded->uncertainty),"old true exposure center lies inside the interval");
    e["envelope_max_coarse"]=1500;
    check(arcturusTiming(frame)&&arcturusTiming(frame)->uncertainty==333541,
          "larger center intervals require the unchanged spatial gate, not stationary exposure settings");
    e["envelope_max_coarse"]=5000;check(!arcturusTiming(frame),"unsupported center interval stays unqualified");
    e["envelope_max_coarse"]=1200;e["envelope_anchor_raw_ns"]=1000000001ULL;
    check(!arcturusTiming(frame),"future anchor cannot grant exposure authority");
    e["envelope_anchor_raw_ns"]=950000000ULL;e["group_release_raw_ns"]=940000000ULL;
    check(!arcturusTiming(frame),"stale transfer cannot reuse an otherwise valid envelope");
    std::puts("PASS EOF-anchored row exposure timing, settled groups and unsupported-mode refusal");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
