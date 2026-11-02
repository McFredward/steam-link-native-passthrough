// Dynamically resolved public APIs; no native runtime binary is distributed.
#pragma once
#define XR_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_TIMESPEC
#include <time.h>
#include <vulkan/vulkan_core.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <string>
namespace room_xr {
#define ROOM_XR_FUNCTIONS(X) \
    X(xrCreateActionSet) \
    X(xrDestroyActionSet) \
    X(xrCreateAction) \
    X(xrCreateActionSpace) \
    X(xrStringToPath) \
    X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) \
    X(xrSyncActions) \
    X(xrGetActionStateBoolean) \
    X(xrGetActionStateFloat) \
    X(xrGetActionStatePose) \
    X(xrAcquireSwapchainImage) \
    X(xrBeginFrame) \
    X(xrBeginSession) \
    X(xrCreateInstance) \
    X(xrCreateReferenceSpace) \
    X(xrCreateSession) \
    X(xrCreateSwapchain) \
    X(xrDestroyInstance) \
    X(xrDestroySession) \
    X(xrDestroySpace) \
    X(xrDestroySwapchain) \
    X(xrEndFrame) \
    X(xrEndSession) \
    X(xrEnumerateEnvironmentBlendModes) \
    X(xrEnumerateInstanceExtensionProperties) \
    X(xrEnumerateReferenceSpaces) \
    X(xrEnumerateSwapchainFormats) \
    X(xrEnumerateSwapchainImages) \
    X(xrEnumerateViewConfigurationViews) \
    X(xrGetInstanceProperties) \
    X(xrGetSystem) \
    X(xrGetSystemProperties) \
    X(xrLocateSpace) \
    X(xrLocateViews) \
    X(xrPollEvent) \
    X(xrReleaseSwapchainImage) \
    X(xrRequestExitSession) \
    X(xrWaitFrame) \
    X(xrWaitSwapchainImage)
#define ROOM_VK_FUNCTIONS(X) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdDraw) \
    X(vkCmdEndRenderPass) \
    X(vkCmdPushConstants) \
    X(vkCmdSetScissor) \
    X(vkCmdSetViewport) \
    X(vkCreateBuffer) \
    X(vkCreateCommandPool) \
    X(vkCreateFramebuffer) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImageView) \
    X(vkCreatePipelineLayout) \
    X(vkCreateRenderPass) \
    X(vkCreateShaderModule) \
    X(vkDestroyBuffer) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDevice) \
    X(vkDestroyFramebuffer) \
    X(vkDestroyImageView) \
    X(vkDestroyInstance) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyRenderPass) \
    X(vkDestroyShaderModule) \
    X(vkDeviceWaitIdle) \
    X(vkEndCommandBuffer) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceQueue) \
    X(vkGetInstanceProcAddr) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkMapMemory) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkResetCommandBuffer) \
    X(vkUnmapMemory)
#define ROOM_DECLARE_API(name) inline PFN_##name name=nullptr;
ROOM_XR_FUNCTIONS(ROOM_DECLARE_API)
ROOM_VK_FUNCTIONS(ROOM_DECLARE_API)
#undef ROOM_DECLARE_API
inline PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr=nullptr;
void bindInstanceApi(XrInstance instance);
class NativeXrLibraries {
    void* xr_=nullptr;void* vk_=nullptr;
public:
    explicit NativeXrLibraries(const std::string& loader);
    ~NativeXrLibraries();
    NativeXrLibraries(const NativeXrLibraries&)=delete;
    NativeXrLibraries& operator=(const NativeXrLibraries&)=delete;
};
} // namespace room_xr
