// Standalone manual prototype. No installation, autostart, or stream setters.
#include "room_scan/camera_api/read_only_policy.h"
#include "room_scan/camera_api/linux_inventory.h"
#include "room_scan/camera_api/buffer_source.h"
#include "room_scan/camera_api/local_api.h"
#include "room_scan/camera_api/stereo_snapshot.h"
#include "room_scan/camera_api/exposure_registers.h"
#include "room_scan/camera_api/owner_lifetime.h"
#include <csignal>
#include <cstdio>
#include <cstring>
namespace {volatile sig_atomic_t stopped=0;void stop(int){stopped=1;}}
int main(int argc,char** argv){
    using namespace room_camera_api;
    try{
        installReadOnlyPolicy();
        if(argc!=2||(std::strcmp(argv[1],"--inspect")&&std::strcmp(argv[1],"--probe-buffers")&&std::strcmp(argv[1],"--serve")))
            throw std::runtime_error("Usage: sfnp-room-camera-native --inspect | --probe-buffers | --serve");
        const auto producer=findProducer();Json cameras=Json::array();
        if(std::strcmp(argv[1],"--inspect")){
            const auto parent=getppid();const auto user=targetUser(producer);ManualOwner owner(user.uid);BorrowedFrames frames(producer);CompletionTrace trace;
            const bool serve=!std::strcmp(argv[1],"--serve");PassiveTrace exposureTrace;ExposureRegisters exposure;
            if(serve){trace.open();exposureTrace.open({"i2c/i2c_write","i2c/i2c_result"});}
            dropPrivileges(user);
            if(prctl(PR_SET_PDEATHSIG,SIGTERM,0,0,0)||getppid()!=parent)throw std::runtime_error("Camera manual session owner disappeared");
            sealReadOnlyMappings();
            auto description=frames.describe();description["setup_privileges_dropped"]=true;description["reader_uid"]=getuid();
            description["manual_owner_monitored"]=owner.monitored();description["reader_pid"]=getpid();
            if(!serve){std::puts(description.dump().c_str());return 0;}
            LocalApi api;description["abstract_socket"]=api.address();std::puts(description.dump().c_str());std::fflush(stdout);
            signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
            StereoSnapshots<BorrowedFrames::Image> snapshots;
            while(!stopped&&!api.stopping()&&frames.alive()&&owner.alive()){
                const auto events=trace.read(producer.pid);exposure.read(exposureTrace,producer.pid);
                for(const auto& event:events)if(auto image=frames.copy(event,api.framesRequested())){
                    image->metadata["source_session"]=api.address();
                    const auto& timing=image->metadata.at("capture_metadata");
                    if(timing.is_object())image->metadata["observed_exposure_registers"]=exposure.settings(image->metadata.at("role"),timing.at("sof_raw_ns"),timing.at("eof_raw_ns"));
                    snapshots.add(std::move(*image));
                }
                api.poll([&](){auto current=description;current["pixel_reads_active"]=api.framesRequested();current["counters"]=frames.describe()["counters"];current["exposure_trace"]=exposure.diagnostics();current["completion_trace"]=trace.diagnostics();return current;},
                    [&](const Json& request){return snapshots.latest(request,monotonicNs());},[&](){return snapshots.pair(monotonicNs());});
            }
            return 0;
        }
        for(const auto& camera:inventory(producer))cameras.push_back(describe(camera));
        const Json report={{"schema","sfnp-native-camera-inventory-v1"},{"producer_pid",producer.pid},{"producer_uid",producer.uid},
            {"read_only_policy",true},{"images_read",false},{"camera_settings_changed",false},{"boot_changes",false},
            {"room_scan_ready",false},{"cameras",cameras}};
        std::puts(report.dump().c_str());return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Native camera prototype: %s\n",error.what());return 1;}
}
