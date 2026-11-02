// Measured mesh and authored elements stay in the same room coordinate space.
#include "room_scan/native_scene_mesh.h"
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
    std::puts("PASS native room vertices, reveal data, quality styles and semantic geometry");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
