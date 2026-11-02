#include "room_scan/native_vision.h"
#include "room_scan/native_arcturus_geometry.h"
#include <cstdio>
#include <random>
using namespace room_native;
void check(bool v,const char* reason){if(!v)throw std::runtime_error(reason);}
int main(int argc,char** argv){try{
    check(argc==2,"An explicit owned native vision module is required");
    NativeStereo matcher;matcher.open(argv[1]);
    std::mt19937 random(719);std::vector<std::uint8_t> left(320*240),right(left.size());
    for(auto& p:left)p=std::uint8_t(random());
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<312;++x)right[y*320+x]=left[y*320+x+8];
    std::array<std::vector<std::uint8_t>,2> support{std::vector<std::uint8_t>(left.size(),1),std::vector<std::uint8_t>(left.size(),1)};
    auto depth=matcher.match(left,right,220,.064f,support);unsigned count=0;double error=0;
    for(float z:depth.depth)if(z>0){++count;error+=std::abs(z-1.76f);}
    check(count>6000&&error/count<.025,"independent metric stereo fixture yields a dense, accurate surface inside the bilateral matching ROI");
    // An independently rendered sloping plane has continuously varying
    // subpixel disparity. A constant-shift fixture cannot detect a reverse
    // speckle filter that accidentally permits only identical disparities.
    for(unsigned y=0;y<240;++y)for(unsigned rx=0;rx<300;++rx){const float lx=(float(rx)+8)/.985f;
        const auto x=unsigned(lx);if(x+1>=320)continue;const float fraction=lx-x;
        right[y*320+rx]=std::uint8_t(std::lround(left[y*320+x]*(1-fraction)+left[y*320+x+1]*fraction));}
    depth=matcher.match(left,right,220,.064f,support);count=0;error=0;
    for(unsigned y=0;y<120;++y)for(unsigned x=0;x<160;++x){const float z=depth.depth[y*160+x];if(z>0){++count;error+=std::abs(z-220*.064f/(8+.015f*(x*2)));}}
    check(count>4000&&error/count<.05,"subpixel stereo preserves a known sloping surface instead of deleting natural disparity gradients");
    for(unsigned y=0;y<240;++y)for(unsigned x=120;x<160;++x)support[0][y*320+x]=0;
    depth=matcher.match(left,right,220,.064f,support);
    for(unsigned y=0;y<120;++y)for(unsigned x=60;x<80;++x)check(depth.depth[y*160+x]==0,"WLS cannot grant geometry in an optically unsupported gap");
    std::fill(support[0].begin(),support[0].end(),1);
    std::fill(left.begin(),left.end(),90);std::fill(right.begin(),right.end(),90);
    depth=matcher.match(left,right,220,.064f,support);
    for(float z:depth.depth)check(z==0,"aggregation cannot invent depth on an untextured wall");
    for(unsigned period:{4u,8u,12u,16u})for(unsigned offset:{0u,3u,5u,8u}){
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){left[y*320+x]=std::uint8_t(x%period<period/2?230:20);
            right[y*320+x]=std::uint8_t((x+offset)%period<period/2?230:20);}
        depth=matcher.match(left,right,220,.064f,support);
        for(float z:depth.depth)check(z==0,"repeated image patterns retain their ambiguity across different phase shifts");
    }
    const V3 point{.21f,.04f,-2.f},lo{.004f,0,0},ro{.076f,0,0};
    const auto hit=intersectStereoRays(lo,point-lo,ro,point-ro);
    check(hit&&length(hit->point-point)<.001f,"capture-time camera translation preserves an independently specified world point");
    check(!intersectStereoRays(lo,{0,0,-1},ro,{0,0,-1}),"parallel rays have no invented depth");
    check(!intersectStereoRays({0,0,0},{0,0,-2},{.064f,.02f,0},{-.064f,0,-2}),"skew rays outside the geometric error bound are refused");
    std::array<ArcturusRectification::RectifiedImage,2> images;
    for(unsigned e=0;e<2;++e){images[e].origins.assign(320*240,e?V3{.072f,0,0}:V3{});images[e].support.assign(320*240,1);}
    StereoResult matched;matched.depth[60*160+90]=220*.064f/(220*.072f/2);matched.confidence[60*160+90]=.9f;
    const auto timed=exposureAlignedDepth(matched,images,Mat{},.064f);
    check(std::abs(timed.depth[60*160+90]-2)<.001f,"actual exposure origins correct the depth bias from an assumed static baseline");
    std::puts("PASS native OpenCV stereo, unsupported holes, flat/periodic ambiguity and capture-time ray origins");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
