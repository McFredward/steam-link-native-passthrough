#include "room_scan/native_view.h"
#include "room_scan/native_present.h"
#include <cstdio>
#include <random>
#include <stdexcept>
using namespace room_native;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    TrackedDevicePose_t tracking{};tracking.bPoseIsValid=tracking.bDeviceIsConnected=true;
    tracking.mDeviceToAbsoluteTracking=HmdMatrix34_t{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    tracking.eTrackingResult=ETrackingResult_TrackingResult_Fallback_RotationOnly;
    check(!tracked(tracking),"rotation-only tracking cannot position room data");
    tracking.eTrackingResult=ETrackingResult_TrackingResult_Running_OK;
    check(tracked(tracking),"valid six-degree tracking is admitted");
    for(float angle:{0.f,.8f,1.6f,2.9f,3.14f,-2.9f}){
        const Mat original=pose({1,2,3},{0,std::sin(angle/2),0,std::cos(angle/2)});
        const auto q=quaternion(original);const Mat recovered=pose({1,2,3},q);
        check(rigid(recovered),"quaternion creates a rigid pose");
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)check(std::abs(original.m[r][c]-recovered.m[r][c])<1e-5,"pose roundtrip");
        const auto identity=multiply(original,inverse(original));check(length(position(identity))<1e-5,"inverse transform");
    }
    std::mt19937 random(42);std::vector<std::uint8_t> left(320*240),right(320*240);
    for(auto& p:left)p=std::uint8_t(random());
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<312;++x)right[y*320+x]=left[y*320+x+8];
    const auto depth=matchStereo(left,right,220,.064f);unsigned valid=0;float sum=0;
    for(float z:depth.depth)if(z){++valid;sum+=z;}
    check(valid>8000,"textured calibrated stereo reconstructs a surface");
    check(std::abs(sum/valid-1.76f)<.02f,"stereo depth has correct metric scale");
    std::array<std::vector<std::uint8_t>,2> invalid{std::vector<std::uint8_t>(320*240),std::vector<std::uint8_t>(320*240)};
    const auto outside=matchStereo(left,right,220,.064f,&invalid);
    for(float z:outside.depth)check(z==0,"rectification padding cannot create a surface");
    std::fill(left.begin(),left.end(),0);std::fill(right.begin(),right.end(),0);
    const auto unknown=matchStereo(left,right,220,.064f);
    for(float z:unknown.depth)check(z==0,"textureless areas remain unknown");
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)left[y*320+x]=right[y*320+x]=std::uint8_t((x%8<4)?230:20);
    const auto repeated=matchStereo(left,right,220,.064f);
    for(float z:repeated.depth)check(z==0,"repeated texture cannot become a unique depth match");
    Canvas image(128,128);image.line(-1e8f,64,1e8f,64,Teal);check(image.pixels.size()==128*128*4,"offscreen raster bounded");
    float x=0,y=0;check(!project({0,0,.1f},Frustum{},128,128,x,y),"behind-eye geometry excluded");
    check(project({0,0,-1},Frustum{},128,128,x,y)&&x==64&&y==64,"projection centre");
    Json state={{"buttons",{{{"x",10},{"y",10},{"w",50},{"h",40},{"enabled",true},{"action","test"}}}}};
    check(hitButton(state,20,20)=="test"&&hitButton(state,70,70).empty(),"controller panel hit bounds");
    check(hitButton(state,964,52)=="close"&&hitButton(Json::object(),964,52)=="close","top-right close works independently of page controls and camera availability");
    check(!hitClose(935,52)&&!hitClose(993,52)&&!hitClose(964,23)&&!hitClose(964,81),"close does not consume neighboring title or status clicks");
    const Mat head=pose({1,1.5f,-.2f},{0,std::sin(.2f),0,std::cos(.2f)});
    const V3 buttonWorld=transform(multiply(head,miniPanelPose()),{(964.f/1024-.5f)*MiniPanelWidth,(.5f-52.f/240)*(MiniPanelWidth*240.f/1024),0});
    Mat controller;const V3 origin=transform(head,{0,-.2f,-.3f});
    const V3 cz=normalized(origin-buttonWorld),cx=normalized(cross({0,1,0},cz)),cy=cross(cz,cx);
    const std::array<V3,4> columns{cx,cy,cz,origin};for(unsigned c=0;c<4;++c){controller.m[0][c]=columns[c].x;controller.m[1][c]=columns[c].y;controller.m[2][c]=columns[c].z;}
    const auto mini=miniPanelCoordinates(head,controller);
    check(mini&&hitClose((*mini)[0],(*mini)[1]),"the controller ray hits X at its actual smaller in-room panel position");
    Json visual={{"world_revision",1},{"pending",nullptr},{"corners",Json::array()},
        {"room",{{"world_visuals_allowed",true},{"elements",Json::array()},
            {"mesh",{{"vertices",{{-.5f,-.5f,-2.f},{.5f,-.5f,-2.f},{0.f,.5f,-2.f}}},
                {"triangles",{{0,1,2}}},{"quality",{"reliable","reliable","reliable"}}}}}}};
    MeshView measured;measured.update(visual,0);Canvas early(128,128),revealed(128,128);
    measured.render(early,Mat{},Frustum{},0);measured.render(revealed,Mat{},Frustum{},.3);
    check(early.pixels!=revealed.pixels,"new measured geometry has a visible reveal animation");
    Canvas occlusion(64,64);std::vector<float> zbuffer;SurfaceCanvas surface(occlusion,zbuffer);
    const SurfacePoint a{8,8,1,{0,0,-1},true},b{56,8,1,{1,0,-1},true},c{32,56,1,{.5f,1,-1},true};
    surface.face(a,b,c,{40,180,220,80},false);const auto front=occlusion.pixels;
    auto farA=a,farB=b,farC=c;farA.z=farB.z=farC.z=3;
    surface.face(farA,farB,farC,{255,50,0,255},false);
    check(occlusion.pixels==front,"a rear room surface cannot draw through a nearer measured surface");
    check(occlusion.pixels[(32*64+32)*4+3]==80,"measured surfaces have a visible transparent fill");
    check(occlusion.pixels[3]==0,"unobserved space stays fully transparent");
    visual["world_revision"]=2;measured.update(visual,1);
    check(measured.firstSeen.size()==3&&measured.firstSeen[0]==0,"existing geometry does not restart its animation on another observation");
    visual["room"]["mesh"]["triangles"].push_back({0,2,1});visual["world_revision"]=3;measured.update(visual,2);
    check(measured.faceFirstSeen.size()==2&&measured.faceFirstSeen[0]==0&&measured.faceFirstSeen[1]==0,"face identity is independent of triangle winding");
    visual["room"]["mesh"]["vertices"].push_back({.5f,.5f,-2.f});visual["room"]["mesh"]["quality"].push_back("reliable");
    visual["world_revision"]=4;measured.update(visual,3);
    visual["room"]["mesh"]["triangles"].push_back({1,3,2});visual["world_revision"]=5;measured.update(visual,4);
    check(measured.firstSeen[3]==3&&measured.faceFirstSeen.back()==4,"new connectivity has its own visible reveal time instead of inheriting old vertex ages");
    visual["room"]["world_visuals_allowed"]=false;measured.update(visual,2);
    check(!measured.allowed&&measured.vertices.empty(),"invalid room tracking removes world geometry");
    TextureOverlayImage upload;Canvas bitmap(32,16);bitmap.rect(0,0,32,16,Teal);unsigned calls=0;
    const auto submit=[&](const Canvas& frame){++calls;check(frame.width==32&&frame.height==16,"fixed GPU image extent");return 0;};
    check(upload.update(bitmap,0,"fixture",submit)&&upload.loaded(),"completed texture copy publishes an image");
    for(unsigned i=1;i<100;++i)upload.update(bitmap,i*.01,"fixture",submit);
    check(calls==1,"an unchanged visible menu submits no repeated textures");
    bitmap.pixels[0]=41;check(!upload.update(bitmap,.01,"fixture",submit)&&calls==1,"rate-limited updates preserve the displayed image");
    check(upload.update(bitmap,1,"fixture",submit)&&calls==2,"a changed hover image publishes without a raw-image load");
    TextureOverlayImage failed;bool refused=false;
    try{failed.update(bitmap,0,"fixture",[](const Canvas&){return 12;});}catch(const std::runtime_error& e){refused=std::string(e.what()).find("code=12")!=std::string::npos;}
    check(refused&&!failed.loaded(),"failed texture submission cannot publish new controls");
    Json displayed={{"page","home"},{"buttons",{{{"action","new"},{"enabled",true}}}}};
    Json changed=displayed;changed["world_revision"]=10;
    check(sameControls(displayed,changed),"unrelated geometry changes do not disable visible controls");
    changed["page"]="prepare";changed["buttons"][0]["action"]="start";
    check(!sameControls(displayed,changed),"a click on the previous page cannot start a hidden camera action");
    std::puts("PASS native stereo, geometry, raster limits and UI hit tests");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
