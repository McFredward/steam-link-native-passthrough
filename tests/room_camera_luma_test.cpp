#include "room_scan/camera_api/luma_layout.h"
#include <cstdio>
using namespace room_camera_api;
namespace {void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}}
int main(){try{
    constexpr unsigned pitch=3328,height=2464;std::vector<std::uint8_t> raw(std::size_t(pitch)*height),image;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<2464;++x)raw[std::size_t(y)*pitch+x]=std::uint8_t((x/16+y/16)%200+20);
    auto layout=examineLuma(raw.data(),raw.size());check(!layout.packed10,"smooth NV12 bytes have no packed low-bit byte");
    copyLuma(raw.data(),raw.size(),layout,image);check(image.size()==616*616,"NV12 uses the entire 2464-pixel image");
    for(unsigned y=0;y<616;++y)for(unsigned x=0;x<616;++x)check(image[y*616+x]==raw[std::size_t(y*4)*pitch+x*4],"NV12 pixel and stride mapping");
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<2464;x+=5){
        auto* p=raw.data()+std::size_t(y)*pitch+x;for(unsigned i=0;i<4;++i)p[i]=100;p[4]=((x/5+y)&1)?0:255;
    }
    layout=examineLuma(raw.data(),raw.size());check(layout.packed10,"packed low bits produce a distinct periodic signature");
    copyLuma(raw.data(),raw.size(),layout,image);check(image.size()==493*616,"RAW10 diagnostic has its own extent");
    for(auto byte:image)check(byte==100,"RAW10 high bytes survive exactly");
    bool rejected=false;try{copyLuma(raw.data(),10,layout,image);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"short source cannot be read");std::puts("PASS independent NV12/RAW10 byte fixtures, full image extents and stride bounds");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
