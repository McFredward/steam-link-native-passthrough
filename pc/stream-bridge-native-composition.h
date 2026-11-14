#pragma once
#include <array>
#include <cstdint>
namespace stream_bridge::sender {
struct CompositionRect {std::uint32_t x=0,y=0,width=0,height=0;};
// Tokens are canonical COM identities retained by the caller, never addresses
// of released objects. This graph supplies no source/format/camera authority.
class NativeComposition {
public:
    static constexpr unsigned Capacity=32;
    bool begin(std::uint64_t serial,std::uint64_t epoch,std::uint32_t width,std::uint32_t height,unsigned eyes)noexcept{
        *this={};serial_=serial;epoch_=epoch;width_=width;height_=height;required_=eyes;
        valid_=serial&&epoch&&width&&height&&width<=16384&&height<=16384&&eyes&&eyes<=255;
        return valid_;
    }
    bool image(std::uintptr_t value)noexcept{
        if(!valid_||!value||(image_&&image_!=value))return refuse();
        image_=value;return true;
    }
    bool draw(unsigned eye,CompositionRect rect)noexcept{
        if(!valid_||!image_||chroma_||planar_||count_==Capacity||!eye||(eye&(eye-1))||(eye&required_)!=eye||!rect.width||!rect.height||rect.x>width_||rect.width>width_-rect.x||rect.y>height_||rect.height>height_-rect.y)return refuse();
        for(unsigned i=0;i<count_;++i){const auto& old=regions_[i];if(rect.x<old.x+old.width&&old.x<rect.x+rect.width&&rect.y<old.y+old.height&&old.y<rect.y+rect.height)return refuse();}
        regions_[count_++]=rect;area_+=std::uint64_t(rect.width)*rect.height;eyes_|=eye;return true;
    }
    bool complete()const noexcept{return valid_&&image_&&eyes_==required_&&area_==std::uint64_t(width_)*height_;}
    bool chroma(std::uintptr_t source,std::uintptr_t output)noexcept{
        if(!complete()||source!=image_||!output||output==image_||chroma_||packed_)return refuse();
        chroma_=output;return true;
    }
    bool directChroma(std::uintptr_t source,std::uintptr_t output)noexcept{
        if(!complete()||source!=image_||!output||output==image_||chroma_||packed_||(planar_&&planar_!=output))return refuse();
        planar_=output;packed_=true;return true;
    }
    bool pack(std::uintptr_t source,std::uintptr_t output)noexcept{
        if(!complete()||!chroma_||source!=chroma_||!output||output==image_||output==chroma_||packed_||(planar_&&planar_!=output))return refuse();
        planar_=output;packed_=true;return true;
    }
    bool luma(std::uintptr_t source,std::uintptr_t output)noexcept{
        if(!complete()||source!=image_||!output||output==image_||output==chroma_||luma_||(planar_&&planar_!=output))return refuse();
        planar_=output;luma_=true;return true;
    }
    bool ready(std::uint64_t serial,std::uint64_t epoch,std::uintptr_t output)const noexcept{return complete()&&packed_&&luma_&&serial==serial_&&epoch==epoch_&&output&&output==planar_;}
    bool contains(std::uintptr_t value)const noexcept{return valid_&&value&&(value==image_||value==chroma_||value==planar_);}
    void invalidate()noexcept{valid_=false;}
private:
    bool refuse()noexcept{valid_=false;return false;}
    std::array<CompositionRect,Capacity> regions_{};unsigned count_=0,eyes_=0,required_=0;
    std::uint64_t serial_=0,epoch_=0,area_=0;std::uint32_t width_=0,height_=0;
    std::uintptr_t image_=0,chroma_=0,planar_=0;bool valid_=false,packed_=false,luma_=false;
};
} // namespace stream_bridge::sender
