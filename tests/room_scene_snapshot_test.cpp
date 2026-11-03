// The real preparation worker publishes immutable geometry and hides lost tracking.
#include "room_scan/native_scene_snapshots.h"
#include <cstdio>
using namespace room_native;
using Json=nlohmann::json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    char surfaceName[]="/tmp/.pc-surfaces-test-XXXXXX";const int surfaceFd=mkstemp(surfaceName);
    check(surfaceFd>=0,"private binary surface fixture");
    struct Cleanup{const char* path;int fd;~Cleanup(){close(fd);unlink(path);}} cleanup{surfaceName,surfaceFd};
    std::vector<SurfaceVertex> surfaceVertices{{12,{-.5f,-.5f,-2},2,{0,0,0}},
        {13,{.5f,-.5f,-2},2,{0,0,0}},{14,{0,.5f,-2},2,{0,0,0}}};
    std::array<std::uint32_t,3> indices{0,1,2};
    const auto writeSurface=[&]{std::vector<std::uint8_t> raw(16+surfaceVertices.size()*20+12);
        std::memcpy(raw.data(),"SFNPMR2\0",8);const std::uint32_t count=surfaceVertices.size(),faces=1;
        std::memcpy(raw.data()+8,&count,4);std::memcpy(raw.data()+12,&faces,4);
        std::memcpy(raw.data()+16,surfaceVertices.data(),surfaceVertices.size()*20);
        std::memcpy(raw.data()+16+surfaceVertices.size()*20,indices.data(),12);
        check(!ftruncate(surfaceFd,0)&&pwrite(surfaceFd,raw.data(),raw.size(),0)==ssize_t(raw.size()),"write coherent binary surface");};
    writeSurface();SurfaceScene surfaces;SceneMeshBuilder measured;Json surfaceState={{"surface_file",surfaceName}};
    surfaces.append(measured,surfaceState,1);
    check(measured.vertices.size()==3&&measured.vertices[0].style==7&&measured.vertices[0].firstSeen==1,"real connectivity renders triangles instead of Gaussian circles");
    measured.vertices.clear();surfaces.append(measured,surfaceState,2);
    check(measured.vertices[0].firstSeen==1,"revisits preserve animation age");
    surfaceVertices[0].p[0]-=.3f;writeSurface();measured.vertices.clear();surfaces.append(measured,surfaceState,3);
    check(measured.vertices[0].firstSeen==3,"recycled identities cannot borrow an unrelated reveal age");
    for(auto& v:surfaceVertices)v.id+=100;
    writeSurface();measured.vertices.clear();surfaces.append(measured,surfaceState,4);
    check(measured.vertices[0].firstSeen==3,"refinement keeps coverage visible when measured topology identities change");
    indices[2]=3;writeSurface();bool refused=false;try{surfaces.append(measured,surfaceState,4);}catch(const std::exception&){refused=true;}
    check(refused,"binary snapshot refuses out of range face indices");indices[2]=2;writeSurface();
    check(!fchmod(surfaceFd,0644),"change fixture permissions");refused=false;
    try{surfaces.append(measured,surfaceState,4);}catch(const std::exception&){refused=true;}
    check(refused,"public binary geometry is refused");check(!fchmod(surfaceFd,0600),"restore private fixture");
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
    state["serial"]=4;state["world_revision"]=2;state["room"]["world_visuals_allowed"]=true;
    state["room"]["mesh"]=nullptr;state["surface_file"]=surfaceName;
    state["gaussian_file"]="/invalid/legacy-file-must-not-be-opened";send();const auto connected=wait(4);
    check(connected->allowed&&connected->vertices->size()==3&&connected->vertices->at(0).style==7,"preparation worker selects measured surfaces and bypasses legacy circles");
    close(input[1]);std::puts("PASS off-frame native geometry preparation, immutable snapshots, reuse and tracking invalidation");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
