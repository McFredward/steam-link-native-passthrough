// The real preparation worker publishes immutable geometry and hides lost tracking.
#include "room_scan/native_scene_snapshots.h"
#include <cstdio>
using namespace room_native;
using Json=nlohmann::json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    int input[2];check(!pipe(input),"snapshot pipe");check(dup2(input[0],STDIN_FILENO)>=0,"snapshot stdin");close(input[0]);
    SceneSnapshots worker(std::chrono::steady_clock::now());
    Json state={{"schema","sfnp-room-dashboard-v1"},{"world_revision",1},{"pending",nullptr},{"corners",Json::array()},
        {"room",{{"world_visuals_allowed",true},{"elements",Json::array()},
            {"mesh",{{"vertices",{{-.5f,-.5f,-2.f},{.5f,-.5f,-2.f},{0.f,.5f,-2.f}}},
                {"triangles",{{0,1,2}}},{"quality",{"reliable","reliable","reliable"}}}}}}};
    const auto send=[&]{const auto raw=state.dump()+"\n";check(write(input[1],raw.data(),raw.size())==ssize_t(raw.size()),"snapshot write");};
    const auto wait=[&](unsigned serial){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(std::chrono::steady_clock::now()<end){const auto value=worker.latest();if(value&&value->state->value("serial",0u)==serial)return value;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        throw std::runtime_error("Snapshot preparation did not complete");};
    state["serial"]=1;send();const auto first=wait(1);
    check(first->allowed&&first->vertices->size()==3,"worker prepares actual native triangles");
    state["serial"]=2;state["title"]="Updated instruction";send();const auto textOnly=wait(2);
    check(textOnly->vertices==first->vertices,"instruction-only updates do not copy or regenerate room geometry");
    state["serial"]=3;state["room"]["world_visuals_allowed"]=false;send();const auto hidden=wait(3);
    check(!hidden->allowed&&hidden->vertices->empty(),"lost tracking publishes empty geometry even without a new revision");
    check(first->allowed&&first->vertices->size()==3,"accepted snapshots stay immutable across later publications");
    close(input[1]);std::puts("PASS off-frame native geometry preparation, immutable snapshots, reuse and tracking invalidation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
