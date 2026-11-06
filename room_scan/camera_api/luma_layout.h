// Diagnostic packing evidence, not calibration or scan authority.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace room_camera_api {
struct LumaLayout {
    bool packed10=false;std::array<double,5> adjacent{};double periodicRatio=0;
    unsigned width()const{return packed10?493:616;}
};
inline LumaLayout examineLuma(const std::uint8_t* bytes,std::size_t count){
    constexpr unsigned pitch=3328,height=2464;
    if(!bytes||count<std::size_t(pitch)*height)throw std::runtime_error("Arcturus luma source is short");
    LumaLayout result;unsigned samples=0;
    for(unsigned y=64;y<2400;y+=64)for(unsigned x=65;x<2390;x+=5){
        const auto* p=bytes+std::size_t(y)*pitch+x;
        for(unsigned j=0;j<5;++j)result.adjacent[j]+=std::abs(int(p[j+1])-int(p[j]));
        ++samples;
    }
    for(auto& value:result.adjacent)value/=samples;
    const auto smooth=(result.adjacent[0]+result.adjacent[1]+result.adjacent[2])/3;
    const auto lowBits=(result.adjacent[3]+result.adjacent[4])/2;
    result.periodicRatio=lowBits/(smooth+1);result.packed10=result.periodicRatio>3&&lowBits>8;return result;
}
inline void copyLuma(const std::uint8_t* bytes,std::size_t count,LumaLayout layout,std::vector<std::uint8_t>& output){
    constexpr unsigned pitch=3328,height=616;
    if(!bytes||count<std::size_t(pitch)*2464)throw std::runtime_error("Arcturus luma source is short");
    output.resize(std::size_t(layout.width())*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<layout.width();++x)
        output[std::size_t(y)*layout.width()+x]=bytes[std::size_t(y*4)*pitch+std::size_t(x)*(layout.packed10?5:4)];
}
inline void copyArea4Luma(const std::uint8_t* bytes,std::size_t count,LumaLayout layout,std::vector<std::uint8_t>& output){
    constexpr unsigned pitch=3328,width=616,height=616;
    if(!bytes||count<std::size_t(pitch)*2464||layout.packed10)throw std::runtime_error("Area sampling requires the complete NV12 luma plane");
    output.resize(std::size_t(width)*height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const auto* pixel=bytes+std::size_t(y*4)*pitch+x*4;unsigned sum=0;
        for(unsigned dy=0;dy<4;++dy)for(unsigned dx=0;dx<4;++dx)sum+=pixel[std::size_t(dy)*pitch+dx];
        output[std::size_t(y)*width+x]=std::uint8_t((sum+8)/16);
    }
}
} // namespace room_camera_api
