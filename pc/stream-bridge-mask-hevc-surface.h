#pragma once
#include <d3d11.h>
#include <array>
#include <memory>

namespace stream_bridge::sender {
// Owned conversion only: full-resolution R8 transmittance -> full-range NV12
// coverage Y=1-T, UV=128. No Valve shader or native RGB target is modified.
class MaskHevcSurfaces {
public:
    MaskHevcSurfaces();~MaskHevcSurfaces();
    MaskHevcSurfaces(const MaskHevcSurfaces&)=delete;
    MaskHevcSurfaces& operator=(const MaskHevcSurfaces&)=delete;
    bool prepareCold(ID3D11Device*,ID3D11Texture2D* transmittance)noexcept;
    std::array<ID3D11Texture2D*,4> textures()const noexcept;
    // Caller holds native multithread protection and an exclusive stage lease,
    // and has verified that NVENC no longer owns this particular destination.
    bool write(ID3D11DeviceContext*,unsigned slot)noexcept;
    void resetCold()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
