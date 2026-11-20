#pragma once
#include <openxr/openxr.h>
#include <cstdint>
#include <functional>
#include <vector>

// What the OpenXR layer submits to SteamVR for one xrEndFrame, decided without
// graphics or I/O. SteamVR shows OPAQUE only; the Frame composes
// E+(1-A)*camera from the projection's own alpha once the sender admits this
// process, so the layer never changes pixels: it only removes what SteamVR
// cannot show and decides whether the frame really carries coverage alpha.
namespace stream_bridge::openxr {
enum class Graphics : unsigned { Unknown, D3D11, D3D12, Vulkan, OpenGL };
struct SwapchainShape {std::int64_t format=0;std::uint32_t arraySize=1,mipCount=1,sampleCount=1;};
struct PassthroughLayerState {bool known=false,running=false;float opacity=1;};
enum class FrameRefusal : unsigned { None, NotRequested, LayerAboveContent, NoProjection, AlphaIgnored, Unpremultiplied, SwapchainShape, SwapchainFormat, Count };
inline const char* refusalName(FrameRefusal value)noexcept{
    constexpr const char* names[]{"none","not requested","passthrough layer above content","bottom layer is not a projection",
        "projection without source alpha","unpremultiplied alpha","swapchain is not one 2D level/sample","swapchain is not 8-bit RGBA/BGRA"};
    return unsigned(value)<unsigned(FrameRefusal::Count)?names[unsigned(value)]:"unknown";
}
// The sender admits 8-bit RGBA/BGRA (UNORM or sRGB) only.
inline bool ordinaryRgba(Graphics api,std::int64_t format)noexcept{
    switch(api){
    case Graphics::D3D11:case Graphics::D3D12:return format==28||format==29||format==87||format==91; // DXGI R8G8B8A8/B8G8R8A8 UNORM/SRGB
    case Graphics::Vulkan:return format==37||format==43||format==44||format==50; // VK_FORMAT R8G8B8A8/B8G8R8A8 UNORM/SRGB
    case Graphics::OpenGL:return format==0x8058||format==0x8C43; // GL_RGBA8, GL_SRGB8_ALPHA8
    default:return false;
    }
}
// Two-call idiom result with ALPHA_BLEND appended after the runtime's modes.
inline XrResult withAlphaBlend(const std::vector<XrEnvironmentBlendMode>& runtime,std::uint32_t capacity,std::uint32_t* count,XrEnvironmentBlendMode* modes)noexcept{
    if(!count)return XR_ERROR_VALIDATION_FAILURE;
    bool present=false;for(auto mode:runtime)present|=mode==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    const auto total=std::uint32_t(runtime.size()+(present?0:1));*count=total;
    if(!capacity)return XR_SUCCESS;
    if(capacity<total||!modes)return XR_ERROR_SIZE_INSUFFICIENT;
    for(std::uint32_t i=0;i<runtime.size();++i)modes[i]=runtime[i];
    if(!present)modes[runtime.size()]=XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    return XR_SUCCESS;
}
struct FramePlan {
    std::vector<const XrCompositionLayerBaseHeader*> layers;
    XrEnvironmentBlendMode mode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool requested=false,coverage=false;FrameRefusal refusal=FrameRefusal::NotRequested;
};
// alphaBlend/fbPassthrough say which of them the layer provides (the runtime
// lacks them); anything the runtime provides itself passes through untouched.
inline FramePlan planFrame(const XrFrameEndInfo& info,bool alphaBlend,bool fbPassthrough,Graphics api,
    const std::function<PassthroughLayerState(XrPassthroughLayerFB)>& passthrough,
    const std::function<bool(XrSwapchain,SwapchainShape&)>& swapchain){
    FramePlan plan;plan.mode=info.environmentBlendMode;
    const bool blend=alphaBlend&&info.environmentBlendMode==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    if(blend)plan.mode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool below=false,above=false,content=false;
    for(std::uint32_t i=0;i<info.layerCount;++i){
        const auto* layer=info.layers?info.layers[i]:nullptr;if(!layer)continue;
        if(fbPassthrough&&layer->type==XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB){
            const auto state=passthrough(reinterpret_cast<const XrCompositionLayerPassthroughFB*>(layer)->layerHandle);
            if(state.known&&state.running&&state.opacity>0){if(content)above=true;else below=true;}
            continue; // SteamVR knows no passthrough layer.
        }
        content=true;plan.layers.push_back(layer);
    }
    plan.requested=blend||below||above;
    if(!plan.requested)return plan;
    // The Frame composes the camera under the whole picture, so a camera layer
    // drawn over game content cannot be represented.
    if(above&&!blend){plan.refusal=FrameRefusal::LayerAboveContent;return plan;}
    const auto* bottom=plan.layers.empty()?nullptr:plan.layers.front();
    if(!bottom||bottom->type!=XR_TYPE_COMPOSITION_LAYER_PROJECTION){plan.refusal=FrameRefusal::NoProjection;return plan;}
    if(!(bottom->layerFlags&XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT)){plan.refusal=FrameRefusal::AlphaIgnored;return plan;}
    if(bottom->layerFlags&XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT){plan.refusal=FrameRefusal::Unpremultiplied;return plan;}
    const auto* projection=reinterpret_cast<const XrCompositionLayerProjection*>(bottom);
    for(std::uint32_t v=0;v<projection->viewCount;++v){
        SwapchainShape shape;const auto& image=projection->views[v].subImage;
        if(!swapchain(image.swapchain,shape)||shape.arraySize!=1||shape.mipCount!=1||shape.sampleCount!=1||image.imageArrayIndex){plan.refusal=FrameRefusal::SwapchainShape;return plan;}
        if(!ordinaryRgba(api,shape.format)){plan.refusal=FrameRefusal::SwapchainFormat;return plan;}
    }
    plan.coverage=true;plan.refusal=FrameRefusal::None;return plan;
}
} // namespace stream_bridge::openxr
