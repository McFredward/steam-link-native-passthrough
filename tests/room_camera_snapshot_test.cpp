#include "room_scan/camera_api/stereo_snapshot.h"
#include <cstdio>
using namespace room_camera_api;
namespace {
struct Image {Json metadata;std::vector<std::uint8_t> pixels;};
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
constexpr std::uint64_t now=2000000000ULL;
Image image(bool right,unsigned seq,std::uint64_t sof=1000000000ULL){
    return {{{"schema","sfnp-native-camera-frame-v1"},{"role",right?"passthrough_right":"passthrough_left"},
        {"sensor",right?"arcimx616 0-0010":"arcimx616 0-001a"},{"source_session","fixture-session"},
        {"producer_pid",100},{"producer_start_ticks","123"},{"sequence",seq},{"copy_end_ns",now},
        {"kernel_role_verified",true},{"queue_mapping_exact",true},{"copy_stable",true},
        {"encoding","nv12-u8-luma-candidate"},{"width",616},{"height",616},
        {"capture_metadata",{{"sof_raw_ns",sof},{"previous_sof_raw_ns",sof-16666667u},
            {"capture_clock","CLOCK_MONOTONIC_RAW"},{"v4l_timestamp_matches",true}}}},std::vector<std::uint8_t>(616*616,std::uint8_t(seq))};
}
}
int main(){try{
    StereoSnapshots<Image> snapshots;snapshots.add(image(false,1));
    check(snapshots.pair(now).value("status","")=="no-fresh-pair","one eye cannot establish a stereo pair");
    snapshots.add(image(true,2,1016666667ULL));
    check(snapshots.pair(now).value("status","")=="no-fresh-pair","arrival order cannot pair adjacent exposures");
    snapshots.add(image(false,2,1016666667ULL));const auto pair=snapshots.pair(now);const auto token=pair.at("pair_token");
    const auto request=Json{{"sensor","arcimx616 0-001a"},{"pair_token",token}};
    check(snapshots.latest(request,now)->pixels.front()==2,"exact snapshot bytes");
    snapshots.add(image(false,3,1033333334ULL));snapshots.add(image(true,3,1033333340ULL));
    check(snapshots.latest(request,now)->pixels.front()==2,"new exposure never changes an in-flight snapshot");
    snapshots.add(image(false,4,1050000000ULL));snapshots.add(image(true,4,1050000000ULL));
    check(!snapshots.latest(request,now),"evicted snapshot is unavailable instead of substituted");
    check(snapshots.pair(now+500000000u).value("status","")=="no-fresh-pair","stale snapshots have no capture authority");
    StereoSnapshots<Image> mixed;auto a=image(false,1),b=image(true,1);b.metadata["source_session"]="another-session";
    mixed.add(std::move(a));mixed.add(std::move(b));
    check(mixed.pair(now).value("status","")=="no-fresh-pair","eyes from different producers cannot pair");
    StereoSnapshots<Image> unknown;b=image(true,1);b.metadata["kernel_role_verified"]=false;
    unknown.add(image(false,1));unknown.add(std::move(b));
    check(unknown.pair(now).value("status","")=="no-fresh-pair","unknown eye identity cannot establish a pair");
    std::puts("PASS exact stereo snapshots, source/eye/time isolation, bounded retention and expiry");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
