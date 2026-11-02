// Standalone manual prototype. No installation, autostart, or stream setters.
#include "room_scan/camera_api/read_only_policy.h"
#include "room_scan/camera_api/linux_inventory.h"
#include "room_scan/camera_api/buffer_source.h"
#include "room_scan/camera_api/local_api.h"
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
            const auto parent=getppid();const auto user=targetUser(producer);BorrowedFrames frames(producer);CompletionTrace trace;
            const bool serve=!std::strcmp(argv[1],"--serve");if(serve)trace.open();
            dropPrivileges(user);
            if(prctl(PR_SET_PDEATHSIG,SIGTERM,0,0,0)||getppid()!=parent)throw std::runtime_error("Camera manual session owner disappeared");
            sealReadOnlyMappings();
            auto description=frames.describe();description["setup_privileges_dropped"]=true;description["reader_uid"]=getuid();
            if(!serve){std::puts(description.dump().c_str());return 0;}
            LocalApi api;description["abstract_socket"]=api.address();std::puts(description.dump().c_str());std::fflush(stdout);
            signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);
            std::map<std::string,BorrowedFrames::Image> latest;
            while(!stopped&&!api.stopping()&&frames.alive()){
                for(const auto& event:trace.read(producer.pid))if(auto image=frames.copy(event)){
                    image->metadata["source_session"]=api.address();
                    latest.insert_or_assign(image->metadata.at("sensor").get<std::string>(),std::move(*image));
                }
                api.poll([&](){auto current=description;current["counters"]=frames.describe()["counters"];current["completion_trace"]=trace.diagnostics();return current;},[&](const std::string& sensor)->const BorrowedFrames::Image*{
                    const auto found=latest.find(sensor);return found!=latest.end()&&monotonicNs()-found->second.metadata.at("copy_end_ns").get<std::uint64_t>()<500000000u?&found->second:nullptr;
                });
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
