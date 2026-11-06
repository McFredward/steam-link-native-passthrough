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
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<2464;++x)raw[std::size_t(y)*pitch+x]=((x+y)&1)?200:20;
    copyLuma(raw.data(),raw.size(),layout,image);check(image.front()==20&&image.back()==20,"point sampling aliases the checkerboard");
    copyArea4Luma(raw.data(),raw.size(),layout,image);
    for(auto byte:image)check(byte==110,"area sampling retains mean energy at every phase");
    copyAreaLuma(raw.data(),raw.size(),layout,image,2);
    check(image.size()==1232*1232,"two-pixel area sampling preserves twice the measured resolution");
    for(auto byte:image)check(byte==110,"two-pixel sampling retains every checkerboard phase");
    for(unsigned y=0;y<1232;++y)for(unsigned x=0;x<1232;++x)image[std::size_t(y)*1232+x]=std::uint8_t((x*7+y*11)%251);
    for(unsigned tile=0;tile<4;++tile){const auto part=area2Tile(image,tile);
        for(unsigned y=0;y<616;++y)for(unsigned x=0;x<616;++x)
            check(part[std::size_t(y)*616+x]==image[std::size_t((tile/2)*616+y)*1232+(tile%2)*616+x],"tiles preserve exact full-image coordinates without blending boundaries");}
    bool badTile=false;try{area2Tile(image,4);}catch(const std::runtime_error&){badTile=true;}
    check(badTile,"out-of-image tile cannot be read");
    std::fill(raw.begin(),raw.end(),0);raw[0]=255;raw[std::size_t(height-1)*pitch+2463]=255;
    copyArea4Luma(raw.data(),raw.size(),layout,image);
    check(image.front()==16&&image.back()==16,"area sampling includes all sixteen pixels and final row/column");
    check(image[1]==0&&image[616]==0,"area sampling does not blend neighbouring output cells");
    copyAreaLuma(raw.data(),raw.size(),layout,image,2);check(image.front()==64&&image.back()==64,"two-pixel area sampling includes first and last native photosites");
    for(const auto* source:{raw.data(),static_cast<std::uint8_t*>(nullptr)}){
        bool rejected=false;try{copyArea4Luma(source,10,layout,image);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"area sampling refuses incomplete or missing source storage");
    }
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<2464;x+=5){
        auto* p=raw.data()+std::size_t(y)*pitch+x;for(unsigned i=0;i<4;++i)p[i]=100;p[4]=((x/5+y)&1)?0:255;
    }
    layout=examineLuma(raw.data(),raw.size());check(layout.packed10,"packed low bits produce a distinct periodic signature");
    bool areaRejected=false;try{copyArea4Luma(raw.data(),raw.size(),layout,image);}catch(const std::runtime_error&){areaRejected=true;}
    check(areaRejected,"area sampling cannot reinterpret packed RAW10 as NV12");
    copyLuma(raw.data(),raw.size(),layout,image);check(image.size()==493*616,"RAW10 diagnostic has its own extent");
    for(auto byte:image)check(byte==100,"RAW10 high bytes survive exactly");
    bool rejected=false;try{copyLuma(raw.data(),10,layout,image);}catch(const std::runtime_error&){rejected=true;}
    check(rejected,"short source cannot be read");std::puts("PASS independent NV12/RAW10 byte fixtures, full image extents and stride bounds");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
