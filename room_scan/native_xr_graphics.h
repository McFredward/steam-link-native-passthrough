// Copyright 2026 Arcturus Industries. SPDX-License-Identifier: MIT

#pragma once

#include "native_xr_api.h"
#include "native_scene_mesh.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace room_xr {
class SceneGraphics
{
  public:
    SceneGraphics();
    ~SceneGraphics();
    SceneGraphics(const SceneGraphics &) = delete;
    SceneGraphics &operator=(const SceneGraphics &) = delete;

    void initialize(XrInstance instance, XrSystemId system);
    const XrGraphicsBindingVulkan2KHR &graphicsBinding() const;
    void createSwapchains(XrInstance instance, XrSystemId system, XrSession session);

    size_t eyeCount() const;
    XrSwapchain swapchain(size_t eye) const;
    XrSwapchain depthSwapchain(size_t eye) const;
    void setRoomPose(const XrPosef& pose);
    void upload(const std::vector<room_native::SceneVertex>& vertices,bool pointer=false);
    uint32_t width(size_t eye) const;
    uint32_t height(size_t eye) const;
    void render(size_t eye, uint32_t imageIndex, uint32_t depthIndex, const XrView &view, float time,bool visible=true,float revealStart=0);
    void destroySwapchains();
    void shutdown();

  private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace room_xr
