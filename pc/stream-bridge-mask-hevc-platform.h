#pragma once
// Internal OS seam. Only the production Win32 adapter and host tests provide
// this implementation; it is never a source/protocol authority boundary.
#include <cstdint>
struct ID3D11Device;
struct ID3D11Texture2D;
namespace stream_bridge::sender::detail {
struct MaskHevcPlatform {
    bool (*textureMatches)(ID3D11Device*,ID3D11Texture2D*,std::uint32_t,std::uint32_t)noexcept=nullptr;
    void (*retainDevice)(ID3D11Device*)noexcept=nullptr;
    void (*releaseDevice)(ID3D11Device*)noexcept=nullptr;
    void (*retainTexture)(ID3D11Texture2D*)noexcept=nullptr;
    void (*releaseTexture)(ID3D11Texture2D*)noexcept=nullptr;
    void* (*createEvent)()noexcept=nullptr;
    void (*closeEvent)(void*)noexcept=nullptr;
    bool (*resetEvent)(void*)noexcept=nullptr;
    // 1=signaled,0=pending,-1=failed. poll never waits; wait is cold flush only.
    int (*pollEvent)(void*)noexcept=nullptr;
    bool (*waitEventCold)(void*)noexcept=nullptr;
};
#if defined(SFNP_MASK_HEVC_HOST_TEST)
MaskHevcPlatform maskHevcTestPlatform()noexcept;
#endif
}
