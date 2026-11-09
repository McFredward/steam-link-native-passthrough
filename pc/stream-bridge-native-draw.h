#pragma once
#include <cstdint>
#include <limits>
namespace stream_bridge::sender {
// Public D3D primitive values; the Windows adapter asserts their SDK identity.
constexpr std::uint32_t NativeTriangleList=4,NativeTriangleStrip=5;
// Geometry is REPLAYED with the original VS/PS/buffers/viewport and arguments.
// This only bounds the optional alpha draw. It never identifies an eye, source,
// native image or frame, and never grants format/coverage permission.
inline bool nativeCoverageDrawShape(bool adaptive,std::uint32_t vertices,
    std::uint32_t start,std::uint32_t topology)noexcept {
    if(vertices==4&&start==0)return true; // Preserve every previously admitted quad.
    if(!adaptive)return false;
    if(!vertices||start>std::numeric_limits<std::uint32_t>::max()-vertices)return false;
    return (topology==NativeTriangleList&&(vertices==3||vertices==6))||
           (topology==NativeTriangleStrip&&(vertices==3||vertices==4));
}
} // namespace stream_bridge::sender
