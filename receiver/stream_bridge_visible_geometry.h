#pragma once
#include <array>
#include <cstdint>
namespace stream_bridge_native {
// Public raw CAPTURE allocation and current decoder COMPOSE rectangle, in
// pixels. These facts are retained by the exact capture lease, never guessed
// from a resolution ratio or received from the PC coverage transport.
struct VisibleGeometry {
    std::uint32_t allocationWidth=0,allocationHeight=0,left=0,top=0,width=0,height=0;
    bool known=false;
    bool valid()const noexcept{
        return known&&allocationWidth&&allocationHeight&&allocationWidth<=16384&&allocationHeight<=16384&&
            width&&height&&left<allocationWidth&&top<allocationHeight&&width<=allocationWidth-left&&height<=allocationHeight-top;
    }
    bool operator==(const VisibleGeometry& other)const noexcept{
        return allocationWidth==other.allocationWidth&&allocationHeight==other.allocationHeight&&left==other.left&&
            top==other.top&&width==other.width&&height==other.height&&known==other.known;
    }
    bool mapping(std::uint32_t imageWidth,std::uint32_t imageHeight,std::uint32_t codedWidth,std::uint32_t codedHeight,
        std::array<float,4>& output)const noexcept{
        if(!valid()||imageWidth!=allocationWidth||imageHeight!=allocationHeight||width!=codedWidth||height!=codedHeight)return false;
        output={{float(allocationWidth)/float(width),float(allocationHeight)/float(height),-float(left)/float(width),-float(top)/float(height)}};return true;
    }
};
}
