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
    Canvas image(128,128);image.line(-1e8f,64,1e8f,64,Teal);check(image.pixels.size()==128*128*4,"offscreen raster bounded");
    float x=0,y=0;check(!project({0,0,.1f},Frustum{},128,128,x,y),"behind-eye geometry excluded");
    check(project({0,0,-1},Frustum{},128,128,x,y)&&x==64&&y==64,"projection centre");
    Json state={{"buttons",{{{"x",10},{"y",10},{"w",50},{"h",40},{"enabled",true},{"action","test"}}}}};
    check(hitButton(state,20,20)=="test"&&hitButton(state,70,70).empty(),"controller panel hit bounds");
    Json visual={{"world_revision",1},{"pending",nullptr},{"corners",Json::array()},
        {"room",{{"world_visuals_allowed",true},{"elements",Json::array()},
            {"mesh",{{"vertices",{{-.5f,-.5f,-2.f},{.5f,-.5f,-2.f},{0.f,.5f,-2.f}}},
                {"triangles",{{0,1,2}}},{"quality",{"reliable","reliable","reliable"}}}}}}};
    MeshView measured;measured.update(visual,0);Canvas early(128,128),revealed(128,128);
    measured.render(early,Mat{},Frustum{},0);measured.render(revealed,Mat{},Frustum{},.3);
    check(early.pixels!=revealed.pixels,"new measured geometry has a visible reveal animation");
    visual["world_revision"]=2;measured.update(visual,1);
    check(measured.firstSeen.size()==3&&measured.firstSeen[0]==0,"existing geometry does not restart its animation on another observation");
    visual["room"]["world_visuals_allowed"]=false;measured.update(visual,2);
    check(!measured.allowed&&measured.vertices.empty(),"invalid room tracking removes world geometry");
    RawOverlayImage upload;Canvas bitmap(32,16);bitmap.rect(0,0,32,16,Teal);unsigned calls=0;
    const auto submit=[&](void*,unsigned w,unsigned h){++calls;check(w==32&&h==16,"fixed raw image extent");return 0;};
    check(upload.update(bitmap,0,"fixture",submit)&&upload.pending(),"first raw image waits for asynchronous completion");
    const auto retained=upload.retained();
    for(unsigned i=1;i<100;++i){bitmap.pixels[0]=std::uint8_t(i);upload.update(bitmap,i*.01,"fixture",submit);}
    check(calls==1&&upload.retained()==retained,"a slow consumer cannot queue images or overwrite its retained bytes");
    upload.complete(true,"fixture");
    check(upload.update(bitmap,1,"fixture",submit)&&calls==2,"completion permits the latest coalesced image");
    upload.complete(true,"fixture");
    for(unsigned i=2;i<100;++i)upload.update(bitmap,i,"fixture",submit);
    check(calls==2,"an unchanged visible menu submits no repeated raw images");
    bitmap.pixels[1]=41;upload.update(bitmap,100,"fixture",submit);bool refused=false;
    try{upload.check(111,"fixture");}catch(const std::runtime_error&){refused=true;}
    check(refused&&calls==3,"missing completion expires without more uploads");
    RawOverlayImage failed;refused=false;
    try{failed.update(bitmap,0,"fixture",[](void*,unsigned,unsigned){return 12;});}catch(const std::runtime_error& e){refused=std::string(e.what()).find("code=12")!=std::string::npos;}
    check(refused&&!failed.pending(),"native submission errors retain their numeric diagnostic");
    Json displayed={{"page","home"},{"buttons",{{{"action","new"},{"enabled",true}}}}};
    Json changed=displayed;changed["world_revision"]=10;
    check(sameControls(displayed,changed),"unrelated geometry changes do not disable visible controls");
    changed["page"]="prepare";changed["buttons"][0]["action"]="start";
    check(!sameControls(displayed,changed),"a click on the previous page cannot start a hidden camera action");
    std::puts("PASS native stereo, geometry, raster limits and UI hit tests");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
