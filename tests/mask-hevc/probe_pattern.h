#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>
namespace sfnp_probe {
constexpr unsigned Width=1152,Height=4608,Frames=96,Fps=90;
// Owned synthetic transmittance data only: static thin foliage/holes, fractional
// fog and moving rain. Integer math makes the source identical across modes/OS.
inline std::uint32_t hash(std::uint32_t v)noexcept{v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
inline void pattern(unsigned frame,std::vector<std::uint8_t>& nv12){
    if(frame>=Frames)throw std::runtime_error("Synthetic frame index exceeds bounded sequence");
    nv12.assign(std::size_t(Width)*Height*3/2,128);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
        const unsigned localY=y%(Height/4),moving=(localY+frame*7)%1152;
        const int dx=int(x%19)-9,dy=int(localY%23)-11;
        unsigned a=0;
        if(x>Width/8&&x<Width*7/8&&localY>64&&localY<1088){
            const auto leaf=hash((x/19)*257+(localY/23));
            if((leaf&3)&&dx*dx*2+dy*dy<110)a=255;
            if((x+localY*3)%97<2)a=255;
            // Exact fractional values, never thresholded to make a test pass.
            if(x>Width/2&&localY>350&&localY<800){const unsigned t=(x+localY/3)%256;a=std::max(a,t);}
            if((hash(x/2)&31)<5&&moving%79<13)a=std::max(a,80+(hash(x+moving*113)&127));
        }
        if(localY<4)a=x%256; // Explicit all-256-values/range control.
        nv12[std::size_t(y)*Width+x]=static_cast<std::uint8_t>(a);
    }
}
inline std::uint64_t fingerprint(const std::vector<std::uint8_t>& bytes)noexcept{
    std::uint64_t v=14695981039346656037ull;for(auto b:bytes){v^=b;v*=1099511628211ull;}return v;
}
} // namespace sfnp_probe
