// Copyright 2026 Arcturus Industries. SPDX-License-Identifier: MIT

#include "native_xr_graphics.h"
#include "room_scene_shaders.h"

#include <xr_linear.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <array>
#include <stdexcept>

namespace room_xr {
namespace
{

void checkXr(XrResult result, const char *operation)
{
    if (XR_FAILED(result))
    {
        std::fprintf(stderr, "%s failed with XrResult %d\n", operation, result);
        throw std::runtime_error("Native XR graphics operation failed");
    }
}

void checkVk(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "%s failed with VkResult %d\n", operation, result);
        throw std::runtime_error("Native XR graphics operation failed");
    }
}

#define CHECK_XR(call) checkXr(call, #call)
#define CHECK_VK(call) checkVk(call, #call)

template <typename Function> Function xrFunction(XrInstance instance, const char *name)
{
    PFN_xrVoidFunction function = nullptr;
    CHECK_XR(xrGetInstanceProcAddr(instance, name, &function));
    return reinterpret_cast<Function>(function);
}

uint32_t findGraphicsQueue(VkPhysicalDevice physicalDevice)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    if(!count||count>64)throw std::runtime_error("Native Vulkan queue count exceeds budget");
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
    for (uint32_t index = 0; index < count; ++index)
    {
        if (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return index;
    }
    std::fprintf(stderr, "No Vulkan graphics queue is available\n");
    throw std::runtime_error("Native XR graphics operation failed");
}

VkShaderModule loadShader(VkDevice device,const uint32_t* code,std::size_t bytes){
    if(!code||!bytes||bytes>65536||bytes%4)throw std::runtime_error("Invalid embedded room shader");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize=bytes;info.pCode=code;
    VkShaderModule module=VK_NULL_HANDLE;CHECK_VK(vkCreateShaderModule(device,&info,nullptr,&module));return module;
}

} // namespace

struct SceneGraphics::Impl
{
    struct Eye
    {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<XrSwapchainImageVulkanKHR> images;
        std::vector<VkImageView> colorViews;
        std::vector<VkFramebuffer> framebuffers;
        XrSwapchain depthSwapchain = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageVulkanKHR> depthImages;
        std::vector<VkImageView> depthViews;
    };

    XrInstance xrInstance = XR_NULL_HANDLE;
    XrPosef roomPose{{0,0,0,1},{0,0,0}};
    struct Vertices {VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* mapped=nullptr;uint32_t count=0;std::size_t capacity=0;};
    Vertices mesh,pointers;
    XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline depthPipeline = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    std::vector<Eye> eyes;

    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const
    {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        CHECK_VK(vkCreateImageView(device, &info, nullptr, &view));
        return view;
    }

    uint32_t findMemoryType(uint32_t allowedTypes) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
        for (uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        {
            if ((allowedTypes & (1u << index)) &&
                (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                return index;
            }
        }
        std::fprintf(stderr, "No device-local Vulkan memory type is available\n");
        throw std::runtime_error("Native XR graphics operation failed");
    }

    void createRenderPass()
    {
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1] = attachments[0];
        attachments[1].format = depthFormat;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        const VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color;
        subpass.pDepthStencilAttachment = &depth;

        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 2;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        CHECK_VK(vkCreateRenderPass(device, &info, nullptr, &renderPass));
    }

    void createPipeline()
    {
        struct PushConstants
        {
            XrMatrix4x4f mvp;
            float time;
            float revealStart;
        };
        VkPushConstantRange pushConstants{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                          0, sizeof(PushConstants)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstants;
        CHECK_VK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout));

        const VkShaderModule vertex = loadShader(device,RoomSceneVertexSpv,sizeof(RoomSceneVertexSpv));
        const VkShaderModule fragment = loadShader(device,RoomSceneFragmentSpv,sizeof(RoomSceneFragmentSpv));
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        const VkVertexInputBindingDescription vertexBinding{0,sizeof(room_native::SceneVertex),VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription,5> attributes{{
            {0,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(room_native::SceneVertex,position)},
            {1,0,VK_FORMAT_R32G32B32A32_SFLOAT,offsetof(room_native::SceneVertex,color)},
            {2,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(room_native::SceneVertex,barycentric)},
            {3,0,VK_FORMAT_R32_SFLOAT,offsetof(room_native::SceneVertex,firstSeen)},
            {4,0,VK_FORMAT_R32_SFLOAT,offsetof(room_native::SceneVertex,style)}}};
        vertexInput.vertexBindingDescriptionCount=1;vertexInput.pVertexBindingDescriptions=&vertexBinding;
        vertexInput.vertexAttributeDescriptionCount=attributes.size();vertexInput.pVertexAttributeDescriptions=attributes.data();
        VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rasterizer{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask = 0xf;
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blending.attachmentCount = 1;
        blending.pAttachments = &colorBlend;
        const VkDynamicState dynamicStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &inputAssembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &rasterizer;
        info.pMultisampleState = &multisampling;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        colorBlend.colorWriteMask=0;colorBlend.blendEnable=VK_FALSE;
        const VkResult depthResult=vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&depthPipeline);
        colorBlend.colorWriteMask=0xf;colorBlend.blendEnable=VK_TRUE;
        depth.depthWriteEnable=VK_FALSE;depth.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        const VkResult result =
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        vkDestroyShaderModule(device, fragment, nullptr);
        vkDestroyShaderModule(device, vertex, nullptr);
        CHECK_VK(depthResult);
        CHECK_VK(result);
    }
};

SceneGraphics::SceneGraphics() : _impl(std::make_unique<Impl>())
{
}
SceneGraphics::~SceneGraphics()
{
    shutdown();
}

void SceneGraphics::initialize(XrInstance xrInstance, XrSystemId system)
{
    _impl->xrInstance = xrInstance;
    auto getRequirements = xrFunction<PFN_xrGetVulkanGraphicsRequirements2KHR>(
        xrInstance, "xrGetVulkanGraphicsRequirements2KHR");
    XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
    CHECK_XR(getRequirements(xrInstance, system, &requirements));
    const XrVersion apiVersion =
        std::max<XrVersion>(requirements.minApiVersionSupported, XR_MAKE_VERSION(1, 1, 0));
    if (apiVersion > requirements.maxApiVersionSupported)
    {
        std::fprintf(stderr,
                     "The runtime cannot provide the Vulkan version required by this sample\n");
        throw std::runtime_error("Native XR graphics operation failed");
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Room Setup";
    appInfo.apiVersion =
        VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(apiVersion), XR_VERSION_MINOR(apiVersion), 0);
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;
    XrVulkanInstanceCreateInfoKHR xrInstanceInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrInstanceInfo.systemId = system;
    xrInstanceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrInstanceInfo.vulkanCreateInfo = &instanceInfo;
    VkResult vkResult = VK_SUCCESS;
    CHECK_XR(xrFunction<PFN_xrCreateVulkanInstanceKHR>(xrInstance, "xrCreateVulkanInstanceKHR")(
        xrInstance, &xrInstanceInfo, &_impl->instance, &vkResult));
    CHECK_VK(vkResult);

    XrVulkanGraphicsDeviceGetInfoKHR deviceInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceInfo.systemId = system;
    deviceInfo.vulkanInstance = _impl->instance;
    CHECK_XR(
        xrFunction<PFN_xrGetVulkanGraphicsDevice2KHR>(xrInstance, "xrGetVulkanGraphicsDevice2KHR")(
            xrInstance, &deviceInfo, &_impl->physicalDevice));
    _impl->queueFamily = findGraphicsQueue(_impl->physicalDevice);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = _impl->queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    XrVulkanDeviceCreateInfoKHR xrDeviceInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrDeviceInfo.systemId = system;
    xrDeviceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrDeviceInfo.vulkanPhysicalDevice = _impl->physicalDevice;
    xrDeviceInfo.vulkanCreateInfo = &createInfo;
    CHECK_XR(xrFunction<PFN_xrCreateVulkanDeviceKHR>(xrInstance, "xrCreateVulkanDeviceKHR")(
        xrInstance, &xrDeviceInfo, &_impl->device, &vkResult));
    CHECK_VK(vkResult);
    vkGetDeviceQueue(_impl->device, _impl->queueFamily, 0, &_impl->queue);

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = _impl->queueFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CHECK_VK(vkCreateCommandPool(_impl->device, &poolInfo, nullptr, &_impl->commandPool));
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = _impl->commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    CHECK_VK(vkAllocateCommandBuffers(_impl->device, &commandInfo, &_impl->commandBuffer));

    _impl->binding.instance = _impl->instance;
    _impl->binding.physicalDevice = _impl->physicalDevice;
    _impl->binding.device = _impl->device;
    _impl->binding.queueFamilyIndex = _impl->queueFamily;
    _impl->binding.queueIndex = 0;
}

const XrGraphicsBindingVulkan2KHR &SceneGraphics::graphicsBinding() const
{
    return _impl->binding;
}

void SceneGraphics::createSwapchains(XrInstance instance, XrSystemId system, XrSession session)
{
    uint32_t count = 0;
    CHECK_XR(xrEnumerateSwapchainFormats(session, 0, &count, nullptr));
    if(!count||count>128)throw std::runtime_error("Native XR format count exceeds budget");
    std::vector<int64_t> formats(count);
    CHECK_XR(xrEnumerateSwapchainFormats(session, count, &count, formats.data()));

    // Only accept swapchain formats with an alpha channel. The alpha channel is used
    // when compositing the submitted swapchain images with the passthrough feed.
    for (const VkFormat candidate : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB})
    {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end())
        {
            _impl->colorFormat = candidate;
            break;
        }
    }
    if (_impl->colorFormat == VK_FORMAT_UNDEFINED)
    {
        std::fprintf(stderr, "No RGBA Vulkan swapchain format\n");
        throw std::runtime_error("Native XR graphics operation failed");
    }

    for (const VkFormat candidate : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM})
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(_impl->physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) && std::find(formats.begin(),formats.end(),candidate)!=formats.end())
        {
            _impl->depthFormat = candidate;
            break;
        }
    }
    if (_impl->depthFormat == VK_FORMAT_UNDEFINED)
    {
        std::fprintf(stderr, "No Vulkan depth format is available\n");
        throw std::runtime_error("Native XR graphics operation failed");
    }
    _impl->createRenderPass();

    CHECK_XR(xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr));
    if(count!=2)throw std::runtime_error("Native XR requires two stereo views");
    std::vector<XrViewConfigurationView> views(count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    CHECK_XR(xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, views.data()));
    if (count != 2)
    {
        std::fprintf(stderr, "This sample expects two stereo views\n");
        throw std::runtime_error("Native XR graphics operation failed");
    }

    _impl->eyes.resize(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        auto &eye = _impl->eyes[index];
        eye.width = std::min(1024u,views[index].recommendedImageRectWidth);
        eye.height = std::min(1024u,views[index].recommendedImageRectHeight);
        if(!eye.width||!eye.height)throw std::runtime_error("Native XR view extent is unavailable");
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format = _impl->colorFormat;
        info.sampleCount = 1;
        info.width = eye.width;
        info.height = eye.height;
        info.faceCount = info.arraySize = info.mipCount = 1;
        CHECK_XR(xrCreateSwapchain(session, &info, &eye.swapchain));
        uint32_t imageCount = 0;
        CHECK_XR(xrEnumerateSwapchainImages(eye.swapchain, 0, &imageCount, nullptr));
        if(!imageCount||imageCount>8)throw std::runtime_error("Swapchain image count exceeds budget");
        eye.images.resize(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        CHECK_XR(xrEnumerateSwapchainImages(
            eye.swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(eye.images.data())));

        info.format = _impl->depthFormat;
        info.usageFlags = XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        CHECK_XR(xrCreateSwapchain(session,&info,&eye.depthSwapchain));
        uint32_t depthCount=0;
        CHECK_XR(xrEnumerateSwapchainImages(eye.depthSwapchain,0,&depthCount,nullptr));
        if(!depthCount||depthCount>8||!imageCount||imageCount>8)throw std::runtime_error("Swapchain image count exceeds budget");
        eye.depthImages.resize(depthCount,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        CHECK_XR(xrEnumerateSwapchainImages(eye.depthSwapchain,depthCount,&depthCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.depthImages.data())));
        for(const auto& image:eye.depthImages)eye.depthViews.push_back(
            _impl->createImageView(image.image,_impl->depthFormat,VK_IMAGE_ASPECT_DEPTH_BIT));
        for(const auto& image:eye.images){
            eye.colorViews.push_back(_impl->createImageView(image.image,_impl->colorFormat,VK_IMAGE_ASPECT_COLOR_BIT));
            for(const auto depthView:eye.depthViews){
                const VkImageView attachments[]{eye.colorViews.back(),depthView};
                VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                framebufferInfo.renderPass=_impl->renderPass;framebufferInfo.attachmentCount=2;framebufferInfo.pAttachments=attachments;
                framebufferInfo.width=eye.width;framebufferInfo.height=eye.height;framebufferInfo.layers=1;
                VkFramebuffer framebuffer=VK_NULL_HANDLE;CHECK_VK(vkCreateFramebuffer(_impl->device,&framebufferInfo,nullptr,&framebuffer));
                eye.framebuffers.push_back(framebuffer);
            }
        }
    }
    _impl->createPipeline();
}

size_t SceneGraphics::eyeCount() const
{
    return _impl->eyes.size();
}
XrSwapchain SceneGraphics::swapchain(size_t eye) const
{
    return _impl->eyes[eye].swapchain;
}
uint32_t SceneGraphics::width(size_t eye) const
{
    return _impl->eyes[eye].width;
}
uint32_t SceneGraphics::height(size_t eye) const
{
    return _impl->eyes[eye].height;
}

void SceneGraphics::render(size_t eyeIndex, uint32_t imageIndex, uint32_t depthIndex, const XrView &view, float time,bool visible,float revealStart)
{

    const auto &eye = _impl->eyes.at(eyeIndex);
    if(depthIndex>=eye.depthImages.size()||imageIndex>=eye.images.size())throw std::runtime_error("Swapchain image index differs");
    XrMatrix4x4f projection, camera, inverseCamera, model, viewProjection, mvp;
    const XrVector3f unitScale{1, 1, 1};

    XrMatrix4x4f_CreateProjectionFov(&projection, GRAPHICS_VULKAN, view.fov, 0.05f, 100.0f);
    XrMatrix4x4f_CreateTranslationRotationScale(&camera, &view.pose.position,
                                                &view.pose.orientation, &unitScale);
    XrMatrix4x4f_InvertRigidBody(&inverseCamera, &camera);
    XrMatrix4x4f_CreateTranslationRotationScale(&model, &_impl->roomPose.position, &_impl->roomPose.orientation, &unitScale);
    XrMatrix4x4f_Multiply(&viewProjection, &projection, &inverseCamera);
    XrMatrix4x4f_Multiply(&mvp, &viewProjection, &model);

    CHECK_VK(vkResetCommandBuffer(_impl->commandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VK(vkBeginCommandBuffer(_impl->commandBuffer, &beginInfo));

    VkClearValue clearValues[2]{};
    clearValues[0].color = {{0, 0, 0, 0}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderInfo.renderPass = _impl->renderPass;
    renderInfo.framebuffer = eye.framebuffers.at(imageIndex*eye.depthImages.size()+depthIndex);
    renderInfo.renderArea.extent = {eye.width, eye.height};
    renderInfo.clearValueCount = 2;
    renderInfo.pClearValues = clearValues;
    vkCmdBeginRenderPass(_impl->commandBuffer, &renderInfo, VK_SUBPASS_CONTENTS_INLINE);

    const VkViewport viewport{0, 0, static_cast<float>(eye.width), static_cast<float>(eye.height),
                              0, 1};
    const VkRect2D scissor{{0, 0}, {eye.width, eye.height}};
    vkCmdSetViewport(_impl->commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(_impl->commandBuffer, 0, 1, &scissor);

    struct PushConstants
    {
        XrMatrix4x4f mvp;
        float time;
        float revealStart;
    } pushConstants{mvp, time,revealStart};
    vkCmdPushConstants(_impl->commandBuffer, _impl->pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(pushConstants), &pushConstants);

    const VkDeviceSize offset=0;
    if(visible&&_impl->mesh.count){
        // Resolve nearest measured/authored geometry before blending. The
        // result must not depend on the TSDF block or triangle iteration order.
        vkCmdBindPipeline(_impl->commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,_impl->depthPipeline);
        vkCmdBindVertexBuffers(_impl->commandBuffer,0,1,&_impl->mesh.buffer,&offset);
        vkCmdDraw(_impl->commandBuffer,_impl->mesh.count,1,0,0);
    }
    vkCmdBindPipeline(_impl->commandBuffer,VK_PIPELINE_BIND_POINT_GRAPHICS,_impl->pipeline);
    for(const auto* vertices:{&_impl->mesh,&_impl->pointers})if(visible&&vertices->count){
        vkCmdBindVertexBuffers(_impl->commandBuffer,0,1,&vertices->buffer,&offset);
        vkCmdDraw(_impl->commandBuffer,vertices->count,1,0,0);
    }

    vkCmdEndRenderPass(_impl->commandBuffer);

    CHECK_VK(vkEndCommandBuffer(_impl->commandBuffer));

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_impl->commandBuffer;
    CHECK_VK(vkQueueSubmit(_impl->queue, 1, &submitInfo, VK_NULL_HANDLE));

    CHECK_VK(vkQueueWaitIdle(_impl->queue));
}

void SceneGraphics::destroySwapchains()
{
    if (!_impl || !_impl->device)return;
    vkDeviceWaitIdle(_impl->device);
    for (auto &eye : _impl->eyes)
    {
        for (const auto framebuffer : eye.framebuffers)
            vkDestroyFramebuffer(_impl->device, framebuffer, nullptr);
        for (const auto view : eye.colorViews)
            vkDestroyImageView(_impl->device, view, nullptr);
        for(auto view:eye.depthViews)vkDestroyImageView(_impl->device,view,nullptr);
        if(eye.depthSwapchain)xrDestroySwapchain(eye.depthSwapchain);
        if (eye.swapchain)
            xrDestroySwapchain(eye.swapchain);
    }
    _impl->eyes.clear();

}

void SceneGraphics::shutdown()
{
    if (!_impl)return;
    if(!_impl->device){if(_impl->instance)vkDestroyInstance(_impl->instance,nullptr);_impl->instance=VK_NULL_HANDLE;return;}
    destroySwapchains();
    for(auto* vertices:{&_impl->mesh,&_impl->pointers}){
        if(vertices->mapped)vkUnmapMemory(_impl->device,vertices->memory);
        if(vertices->buffer)vkDestroyBuffer(_impl->device,vertices->buffer,nullptr);
        if(vertices->memory)vkFreeMemory(_impl->device,vertices->memory,nullptr);
        *vertices=Impl::Vertices{};
    }
    if (_impl->pipeline)
        vkDestroyPipeline(_impl->device, _impl->pipeline, nullptr);
    if (_impl->depthPipeline)
        vkDestroyPipeline(_impl->device,_impl->depthPipeline,nullptr);
    if (_impl->pipelineLayout)
        vkDestroyPipelineLayout(_impl->device, _impl->pipelineLayout, nullptr);
    if (_impl->renderPass)
        vkDestroyRenderPass(_impl->device, _impl->renderPass, nullptr);
    if (_impl->commandPool)
        vkDestroyCommandPool(_impl->device, _impl->commandPool, nullptr);
    vkDestroyDevice(_impl->device, nullptr);
    vkDestroyInstance(_impl->instance, nullptr);
    _impl->device = VK_NULL_HANDLE;
    _impl->instance = VK_NULL_HANDLE;
}

XrSwapchain SceneGraphics::depthSwapchain(size_t eye)const{return _impl->eyes.at(eye).depthSwapchain;}
void SceneGraphics::setRoomPose(const XrPosef& pose){_impl->roomPose=pose;}
void SceneGraphics::upload(const std::vector<room_native::SceneVertex>& values,bool pointer){
    if(values.size()>room_native::SceneVertexLimit)throw std::runtime_error("Room scene vertex count exceeds budget");
    auto& target=pointer?_impl->pointers:_impl->mesh;
    if(values.empty()){target.count=0;return;}
    const std::size_t bytes=values.size()*sizeof(room_native::SceneVertex);
    if(bytes>64u*1024*1024)throw std::runtime_error("Room scene vertex bytes exceed budget");
    if(bytes>target.capacity){
        CHECK_VK(vkQueueWaitIdle(_impl->queue));
        if(target.mapped)vkUnmapMemory(_impl->device,target.memory);
        if(target.buffer)vkDestroyBuffer(_impl->device,target.buffer,nullptr);
        if(target.memory)vkFreeMemory(_impl->device,target.memory,nullptr);
        target=Impl::Vertices{};
        const std::size_t capacity=(bytes+65535)&~std::size_t(65535);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=capacity;info.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        CHECK_VK(vkCreateBuffer(_impl->device,&info,nullptr,&target.buffer));
        VkMemoryRequirements required{};vkGetBufferMemoryRequirements(_impl->device,target.buffer,&required);
        if(required.size>64u*1024*1024)throw std::runtime_error("Room scene allocation exceeds budget");
        VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(_impl->physicalDevice,&properties);
        uint32_t chosen=properties.memoryTypeCount;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((required.memoryTypeBits&(1u<<i))&&
            (properties.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){chosen=i;break;}
        if(chosen==properties.memoryTypeCount)throw std::runtime_error("Room scene mapped memory unavailable");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=required.size;allocation.memoryTypeIndex=chosen;
        CHECK_VK(vkAllocateMemory(_impl->device,&allocation,nullptr,&target.memory));CHECK_VK(vkBindBufferMemory(_impl->device,target.buffer,target.memory,0));
        CHECK_VK(vkMapMemory(_impl->device,target.memory,0,VK_WHOLE_SIZE,0,&target.mapped));target.capacity=capacity;
    }
    std::memcpy(target.mapped,values.data(),bytes);target.count=uint32_t(values.size());
}
} // namespace room_xr
