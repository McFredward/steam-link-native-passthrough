// Measured mesh and authored elements stay in the same room coordinate space.
#include "room_scan/native_scene_mesh.h"
#include "room_scan/native_gaussian.h"
#include "room_scan/native_scan_guidance.h"
#include <cstdio>
using namespace room_native;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    MeshView view;view.allowed=true;
    view.vertices={{-.5f,0,-2},{.5f,0,-2},{0,1,-2}};view.triangles={{{0,1,2}}};view.quality={0,0,0};view.firstSeen={2,3,4};
    SceneMeshBuilder scene;scene.update(view);check(scene.vertices.size()==3,"only the measured triangle is emitted");
    for(unsigned i=0;i<3;++i){const auto& v=scene.vertices[i];check(v.position[0]==view.vertices[i].x&&v.position[2]==-2,"room vertices do not follow an eye or head pose");
        check(v.barycentric[i]==1&&v.firstSeen==4&&v.style==0,"actual triangle edges and observation time reach the reveal shader");}
    view.faceFirstSeen={8};scene.update(view);
    check(scene.vertices[0].firstSeen==8,"newly reconstructed coverage reveals even when its vertices already existed");
    view.quality={2,2,2};scene.update(view);check(scene.vertices[0].style==2,"only reliable geometry receives the reliable visual style");
    view.quality[1]=1;scene.update(view);check(scene.vertices[0].style==1,"uncertain evidence remains visibly uncertain");
    view.elementFaces.push_back({{{{0,0,-1},{1,0,-1},{1,1,-1}}},true});
    view.pendingEdges.push_back({{0,0,-1},{1,0,-1}});view.corners.push_back({0,0,-1});scene.update(view);
    check(scene.vertices.size()>30&&scene.vertices[3].style==3,"authored planes, outlines and corner handles are real native geometry");
    view.allowed=false;scene.update(view);check(scene.vertices.empty(),"invalid tracking cannot retain visible room geometry");
    char name[]="/tmp/.pc-gaussians-contract-XXXXXX";const int fd=mkstemp(name);
    check(fd>=0,"private Gaussian fixture file");
    struct File {int fd;const char* name;~File(){close(fd);unlink(name);}} file{fd,name};
    std::vector<GaussianRecord> records;bool roundedBelow=false;
    for(unsigned i=0;i<5000;++i){const auto normal=normalized(V3{std::sin(float(i)*.31f),std::cos(float(i)*.17f),.3f});
        const auto guide=std::abs(normal.y)>.9f?V3{1,0,0}:V3{0,1,0};
        const auto tangent=normalized(cross(normal,guide)),u=tangent*.001f,v=cross(normal,tangent)*.001f;
        roundedBelow=roundedBelow||length(u)<.001f||length(v)<.001f;
        records.push_back({i,{0,0,-2,u.x,u.y,u.z,v.x,v.y,v.z,.6f,1}});}
    check(roundedBelow,"float32 tangent axes reproduce the minimum-radius roundoff");
    const auto bytes=records.size()*sizeof(GaussianRecord);
    check(write(fd,records.data(),bytes)==ssize_t(bytes),"write complete private Gaussian fixture");
    GaussianScene gaussians;SceneMeshBuilder patches;
    gaussians.append(patches,{{"gaussian_file",name}},1);
    check(patches.vertices.size()==records.size()*6,"all valid small Gaussians reach the renderer");
    records[0].v[3]=.0009f;records[0].v[4]=records[0].v[5]=0;
    check(pwrite(fd,records.data(),sizeof(GaussianRecord),0)==ssize_t(sizeof(GaussianRecord)),"replace invalid-size fixture");
    bool rejected=false;try{gaussians.append(patches,{{"gaussian_file",name}},2);}catch(const std::exception&){rejected=true;}
    check(rejected,"materially undersized axes remain invalid");
    ScanGuidance guidance;const auto empty=nlohmann::json(std::vector<unsigned>(72,0));
    guidance.configure({{"id","room-a"},{"active",true},{"observed",empty},{"measured",empty}});
    SceneMeshBuilder arrows;const auto instruction=guidance.append(arrows,Mat{},1);
    check(!instruction.empty()&&arrows.vertices.size()==144,"a live head-responsive guide exists before any depth is captured");
    const auto firstArrow=arrows.vertices.front();arrows.vertices.clear();guidance.append(arrows,Mat{},1.6);
    check(firstArrow.color[3]!=arrows.vertices.front().color[3],"the actual guide visibly pulses without camera or model updates");
    guidance.configure({{"id","room-a"},{"active",false},{"observed",empty},{"measured",empty}});
    arrows.vertices.clear();check(guidance.append(arrows,Mat{},2).empty()&&arrows.vertices.empty(),"paused scans hide the guide");
    std::puts("PASS native room vertices, reveal data, quality styles and semantic geometry");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
