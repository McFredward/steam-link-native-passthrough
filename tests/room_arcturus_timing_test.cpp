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
    frame["observed_exposure_registers"]["same_value_since_raw_ns"]=990000000ULL;
    check(!arcturusTiming(frame),"newly changed setting cannot attest current capture");
    frame=fixture();frame["observed_exposure_registers"]["unchanged_through_eof"]=false;
    check(!arcturusTiming(frame),"a setting change during readout is refused");
    frame=fixture();frame["capture_metadata"]["eof_raw_ns"]=1005000000ULL;
    check(!arcturusTiming(frame),"another sensor readout mode is refused");
    frame=fixture();frame["observed_exposure_registers"]["coarse_integration_lines"]=10000;
    check(!arcturusTiming(frame),"long-exposure mode is outside this capture contract");
    std::puts("PASS EOF-anchored row exposure timing, settled groups and unsupported-mode refusal");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
