// Actual owned pipe + Python child, explicitly synthetic observations, no VR.
#include "room_scan/native_pipe.h"
#include <filesystem>
#include <cstdio>

using Json=nlohmann::json;
using namespace room_native;
std::shared_ptr<const Json> waitFor(Backend& backend,const std::string& page,unsigned frames=0){
    for(unsigned n=0;n<200;++n){
        auto state=backend.latest();
        if(state&&state->value("page","")==page){
            if(!frames||(!state->at("room").is_null()&&state->at("room").at("progress").value("accepted_frames",0u)>=frames))return state;
        }
        if(!backend.alive)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("Owned controller pipe did not reach expected state: "+page);
}
int main(int argc,char** argv){
    char temporary[]="/tmp/sfnp-room-pipe-fixture-XXXXXX";
    if(argc!=2||!mkdtemp(temporary))return 2;
    struct Cleanup {const char* path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{temporary};
    signal(SIGPIPE,SIG_IGN);
    try{
        Backend backend;if(!backend.start(argv[1],temporary))throw std::runtime_error("Child launch failed");
        waitFor(backend,"home");backend.send({{"event","action"},{"action","new"}});waitFor(backend,"prepare");
        backend.send({{"event","camera_unavailable"}});
        for(unsigned n=0;n<200&&waitFor(backend,"prepare")->value("notice","").empty();++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(backend.latest()->value("capture_active",true))throw std::runtime_error("Unavailable source started acquisition");
        const Json binding={{"session_id","11111111-1111-4111-a111-111111111111"},
            {"origin_id","22222222-2222-4222-a222-222222222222"},{"calibration_id","33333333-3333-4333-a333-333333333333"}};
        backend.send({{"event","camera_ready"},{"binding",binding}});waitFor(backend,"scan");
        backend.send({{"event","depth"},{"binding",binding},{"sequence",1},{"exposure_ticks",100},
            {"pose",{{"position",{0,0,0}},{"orientation_xyzw",{0,0,0,1}}}},
            {"width",3},{"height",3},{"intrinsics",{20,20,1,1}},
            {"depth_m",{1,1,1,1,1,1,1,1,1}},{"confidence",{1,1,1,1,1,1,1,1,1}}},true);
        waitFor(backend,"scan",1);backend.send({{"event","action"},{"action","review"}});waitFor(backend,"review");
        backend.send({{"event","action"},{"action","save"}});const auto saved=waitFor(backend,"saved");
        if(saved->value("capture_active",true))throw std::runtime_error("Saved room kept acquisition active");
        bool head=false;for(const auto& file:std::filesystem::directory_iterator(temporary))if(file.path().extension()==".head")head=true;
        if(!head)throw std::runtime_error("Saved room was not published");
        backend.stop();std::puts("PASS real native/Python pipe, unavailable source, fixture depth, review/save and owned cleanup");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
