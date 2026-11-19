#pragma once
#include <cstdint>
namespace stream_bridge::sender {
struct SharedTextureShape {
    std::uint32_t width=0,height=0,format=0,samples=0,arraySize=1;
};
// A native request is not a physical texture description. Steam Link packs
// private sample/array metadata into the public descriptor's fourth word;
// that encoding can change independently of DirectMode009. Preserve the word
// verbatim for native calls, but derive samples/arrays from actual D3D11 objects.
// No request alone authorizes a source, slice, sample, frame or mask.
struct SharedTextureRequest {
    std::uint32_t width=0,height=0,format=0;
};
inline bool describeSharedTextureRequest(std::uint32_t width,std::uint32_t height,
    std::uint32_t format,SharedTextureRequest& output)noexcept{
    output={};
    if(!width||width>16384||!height||height>16384||!format)return false;
    output={width,height,format};return true;
}
} // namespace stream_bridge::sender
