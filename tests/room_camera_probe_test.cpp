#include "probe/room_camera_probe_core.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace room_camera_probe;
namespace {
void check(bool okay){if(!okay)throw std::runtime_error("camera inventory check failed");}
class Fake final:public Api {
public:
    int count=2,calls=0;bool present=true;
    void camera(bool& has,int& error)override{has=present;error=0;}
    void properties(std::int32_t& n,int& error,std::int32_t& layout,int& layoutError)override{n=count;error=0;layout=34;layoutError=0;}
    Size size(unsigned)override{++calls;return {0,100,100,40000};}
    Intrinsics intrinsics(unsigned,unsigned)override{++calls;return {0,50,50,50,50};}
    void extrinsics(std::array<Extrinsics,MaxCameras>& out)override{++calls;out[0]={{1,0,0,0,0,1,0,0,0,0,1,0},true};}
};
}
int main(int argc,char**){try{
    Fake api;auto data=inspect(api);const auto json=report(data);
    check(api.calls==10);check(valid(data.sizes[0]));check(valid(data.intrinsics[0][0]));check(valid(data.extrinsics[0]));
    check(json.find("\"room_scan_ready\":false")!=std::string::npos);
    check(json.find("\"arcturus_identity_verified\":false")!=std::string::npos);
    check(json.find("\"camera_images_read\":false")!=std::string::npos);
    api.count=1000000;api.calls=0;data=inspect(api);check(api.calls==3);
    api.present=false;api.calls=0;data=inspect(api);check(api.calls==0);
    auto badSize=Size{0,UINT32_MAX,UINT32_MAX,4};check(!valid(badSize));
    auto badIntrinsic=Intrinsics{0,std::numeric_limits<float>::quiet_NaN(),1,1,1};check(!valid(badIntrinsic));
    Inventory malformed;malformed.count=1;malformed.countError=0;malformed.intrinsics[0][0]=badIntrinsic;
    check(report(malformed).find("nan")==std::string::npos);
    auto reflected=Extrinsics{{-1,0,0,0,0,1,0,0,0,0,1,0},true};check(!valid(reflected));
    if(argc>1)std::fputs(json.c_str(),stdout);else std::puts("PASS bounded public camera inventory and unavailable-data controls");
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
