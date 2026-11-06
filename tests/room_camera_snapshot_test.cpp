#include "room_scan/camera_api/stereo_snapshot.h"
#include "room_scan/camera_api/stereo_copy_schedule.h"
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
void copySchedule(){
    // A valid 60 us stereo skew straddles every 100 ms boundary. Independent
    // bins subsequently select opposite sides of different 16.7 ms exposures.
    StereoSnapshots<Image> oldSnapshots,pairedSnapshots;StereoCopySchedule schedule;
    std::array<std::uint64_t,2> bins{};unsigned copied=0;
    for(unsigned frame=0;frame<=600;++frame){
        const std::uint64_t centre=2000000000ULL+frame*16666667ULL;
        for(unsigned index=0;index<2;++index){
            const unsigned eye=frame%2?1-index:index;
            const auto sof=eye?centre+30000u:centre-30000u;
            if(sof/100000000u!=bins[eye]){
                bins[eye]=sof/100000000u;oldSnapshots.add(image(eye,frame+1,sof));
            }
            if(schedule.wants(eye,sof)){
                check(schedule.copied(eye,sof),"only a completed stable copy consumes the shared budget");
                pairedSnapshots.add(image(eye,frame+1,sof));++copied;
            }
        }
    }
    check(oldSnapshots.pair(now).at("pair_token")=="1","independent bins reproduce a stereo availability stall");
    check(pairedSnapshots.pair(now).at("pair_token")=="101"&&copied==202,"shared schedule retains ten exact pairs per second across bin boundaries");
    const auto pair=pairedSnapshots.pair(now);
    check(pair["members"][0]["sequence"]==pair["members"][1]["sequence"],"selected eyes retain the same actual hardware exposure");
    StereoCopySchedule missing;
    check(!missing.wants(2,1000000000u)&&!missing.wants(0,0),"invalid eye and missing SOF cannot select work");
    check(missing.wants(0,1000000000u)&&missing.wants(0,1000000000u),"a failed copy does not consume the budget");
    check(missing.copied(0,1000000000u),"first completed copy selects an exposure");
    check(!missing.wants(0,1000000000u)&&!missing.copied(0,1000000000u),"duplicate eye is never copied twice");
    check(!missing.wants(1,1016666667u),"a missing mate cannot borrow the following exposure");
    check(!missing.wants(1,999899999u)&&missing.wants(1,999900000u),"matching earlier eye has exactly the existing 100 us bound");
    check(missing.copied(1,1000100000u),"matching later eye has exactly the existing 100 us bound");
    check(!missing.wants(0,1099899999u)&&missing.wants(0,1099900000u),"next exposure becomes eligible within the bounded scheduling tolerance");
    check(missing.copied(1,1100000000u)&&missing.copied(0,1099940000u),"either eye may lead the next pair");
    check(!missing.wants(0,1000000000u),"a delayed old mate never resets the selected exposure");
    StereoCopySchedule orphan;
    check(orphan.copied(0,1000000000u)&&!orphan.wants(0,1016666667u),"missing mate does not cause copies at full sensor rate");
    check(orphan.copied(0,1100000000u)&&orphan.copied(1,1100000060u),"one missing mate does not stall future stereo work");
}
}
int main(){try{
    copySchedule();
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
    std::puts("PASS shared stereo copy scheduling, exact snapshots, source/eye/time isolation, bounded retention and expiry");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
