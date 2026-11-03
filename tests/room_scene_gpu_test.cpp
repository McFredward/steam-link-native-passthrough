// Actual production shaders, Vulkan geometry, color/depth readback; no XR runtime.
#include "room_scan/native_xr_graphics.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <map>
using namespace room_xr;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void success(VkResult result){check(result==VK_SUCCESS,"Real Vulkan fixture operation failed");}
void* loader=nullptr;VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;
template<class T>T entry(const char* name){const auto address=dlsym(loader,name);check(address,"Vulkan fixture entry unavailable");T result;std::memcpy(&result,&address,sizeof(result));return result;}
struct Chain {VkFormat format;std::vector<VkImage> images;std::vector<VkDeviceMemory> memory;};
std::map<XrSwapchain,Chain*> chains;
uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags){VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);
    for(uint32_t i=0;i<p.memoryTypeCount;++i)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("Vulkan fixture memory unavailable");}
XrResult requirements(XrInstance,XrSystemId,XrGraphicsRequirementsVulkanKHR* out){out->minApiVersionSupported=XR_MAKE_VERSION(1,1,0);out->maxApiVersionSupported=XR_MAKE_VERSION(1,3,0);return XR_SUCCESS;}
XrResult createInstance(XrInstance,const XrVulkanInstanceCreateInfoKHR* info,VkInstance* out,VkResult* result){
    *result=entry<PFN_vkCreateInstance>("vkCreateInstance")(info->vulkanCreateInfo,nullptr,out);return XR_SUCCESS;}
XrResult graphicsDevice(XrInstance,const XrVulkanGraphicsDeviceGetInfoKHR* info,VkPhysicalDevice* out){uint32_t count=0;const auto enumerate=entry<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    success(enumerate(info->vulkanInstance,&count,nullptr));check(count>0&&count<=16,"Fixture Vulkan devices");std::vector<VkPhysicalDevice> devices(count);
    success(enumerate(info->vulkanInstance,&count,devices.data()));*out=physical=devices.front();return XR_SUCCESS;}
XrResult createDevice(XrInstance,const XrVulkanDeviceCreateInfoKHR* info,VkDevice* out,VkResult* result){
    *result=entry<PFN_vkCreateDevice>("vkCreateDevice")(info->vulkanPhysicalDevice,info->vulkanCreateInfo,nullptr,out);device=*out;return XR_SUCCESS;}
XrResult getProc(XrInstance,const char* name,PFN_xrVoidFunction* out){
    if(!std::strcmp(name,"xrGetVulkanGraphicsRequirements2KHR"))*out=reinterpret_cast<PFN_xrVoidFunction>(requirements);
    else if(!std::strcmp(name,"xrCreateVulkanInstanceKHR"))*out=reinterpret_cast<PFN_xrVoidFunction>(createInstance);
    else if(!std::strcmp(name,"xrGetVulkanGraphicsDevice2KHR"))*out=reinterpret_cast<PFN_xrVoidFunction>(graphicsDevice);
    else if(!std::strcmp(name,"xrCreateVulkanDeviceKHR"))*out=reinterpret_cast<PFN_xrVoidFunction>(createDevice);
    else return XR_ERROR_FUNCTION_UNSUPPORTED;
    return XR_SUCCESS;
}
XrResult formats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out){*count=2;if(capacity){check(capacity>=2,"Fixture format capacity");out[0]=VK_FORMAT_R8G8B8A8_SRGB;out[1]=VK_FORMAT_D32_SFLOAT;}return XR_SUCCESS;}
XrResult views(XrInstance,XrSystemId,XrViewConfigurationType,uint32_t capacity,uint32_t* count,XrViewConfigurationView* out){*count=2;
    if(capacity){check(capacity>=2,"Fixture stereo capacity");for(unsigned i=0;i<2;++i){out[i].recommendedImageRectWidth=128;out[i].recommendedImageRectHeight=128;out[i].recommendedSwapchainSampleCount=1;}}return XR_SUCCESS;}
XrResult createChain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out){auto chain=new Chain;chain->format=VkFormat(info->format);const bool depth=chain->format==VK_FORMAT_D32_SFLOAT;
    const unsigned count=depth?3:2;
    for(unsigned i=0;i<count;++i){VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=chain->format;image.extent={info->width,info->height,1};
        image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkImage handle;success(entry<PFN_vkCreateImage>("vkCreateImage")(device,&image,nullptr,&handle));VkMemoryRequirements needed{};entry<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(device,handle,&needed);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=needed.size;allocation.memoryTypeIndex=memoryType(needed.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkDeviceMemory memory;success(vkAllocateMemory(device,&allocation,nullptr,&memory));success(entry<PFN_vkBindImageMemory>("vkBindImageMemory")(device,handle,memory,0));chain->images.push_back(handle);chain->memory.push_back(memory);}
    *out=reinterpret_cast<XrSwapchain>(chain);chains[*out]=chain;return XR_SUCCESS;}
XrResult images(XrSwapchain handle,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out){const auto& chain=*chains.at(handle);*count=uint32_t(chain.images.size());
    if(capacity){check(capacity>=*count,"Fixture image capacity");auto* target=reinterpret_cast<XrSwapchainImageVulkanKHR*>(out);for(unsigned i=0;i<*count;++i)target[i].image=chain.images[i];}return XR_SUCCESS;}
XrResult destroyChain(XrSwapchain handle){auto* chain=chains.at(handle);for(unsigned i=0;i<chain->images.size();++i){entry<PFN_vkDestroyImage>("vkDestroyImage")(device,chain->images[i],nullptr);vkFreeMemory(device,chain->memory[i],nullptr);}chains.erase(handle);delete chain;return XR_SUCCESS;}
std::vector<uint8_t> readback(const SceneGraphics& graphics,XrSwapchain handle,unsigned imageIndex){
    const auto& chain=*chains.at(handle);const bool depth=chain.format==VK_FORMAT_D32_SFLOAT;
    const auto binding=graphics.graphicsBinding();VkQueue queue;vkGetDeviceQueue(device,binding.queueFamilyIndex,0,&queue);
    VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};b.size=128*128*4;b.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;VkBuffer buffer;success(vkCreateBuffer(device,&b,nullptr,&buffer));
    VkMemoryRequirements needed{};vkGetBufferMemoryRequirements(device,buffer,&needed);VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=needed.size;
    allocation.memoryTypeIndex=memoryType(needed.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkDeviceMemory memory;success(vkAllocateMemory(device,&allocation,nullptr,&memory));success(vkBindBufferMemory(device,buffer,memory,0));
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};poolInfo.queueFamilyIndex=binding.queueFamilyIndex;VkCommandPool pool;success(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};commandInfo.commandPool=pool;commandInfo.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commandInfo.commandBufferCount=1;VkCommandBuffer command;success(vkAllocateCommandBuffers(device,&commandInfo,&command));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};success(vkBeginCommandBuffer(command,&begin));VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=depth?VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=chain.images.at(imageIndex);barrier.subresourceRange={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    entry<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(command,VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
    VkBufferImageCopy copy{};copy.imageSubresource={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={128,128,1};entry<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(command,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buffer,1,&copy);
    success(vkEndCommandBuffer(command));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;success(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));success(vkQueueWaitIdle(queue));
    void* mapped;success(vkMapMemory(device,memory,0,needed.size,0,&mapped));std::vector<uint8_t> result(128*128*4);std::memcpy(result.data(),mapped,result.size());vkUnmapMemory(device,memory);
    vkDestroyCommandPool(device,pool,nullptr);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,memory,nullptr);return result;
}
}
int main(){try{
    loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);check(loader,"Real Vulkan loader");
#define BIND_VK(name) room_xr::name=entry<PFN_##name>(#name);
    ROOM_VK_FUNCTIONS(BIND_VK)
#undef BIND_VK
    xrGetInstanceProcAddr=getProc;xrEnumerateSwapchainFormats=formats;xrEnumerateViewConfigurationViews=views;
    xrCreateSwapchain=createChain;xrEnumerateSwapchainImages=images;xrDestroySwapchain=destroyChain;
    SceneGraphics graphics;graphics.initialize(reinterpret_cast<XrInstance>(1),1);graphics.createSwapchains(reinterpret_cast<XrInstance>(1),1,reinterpret_cast<XrSession>(1));
    room_native::MeshView mesh;mesh.allowed=true;mesh.vertices={{-.5f,-.5f,-2},{.5f,-.5f,-2},{0,.5f,-2}};mesh.triangles={{{0,1,2}}};mesh.quality={2,2,2};mesh.firstSeen={0,0,0};
    room_native::SceneMeshBuilder builder;builder.update(mesh);graphics.upload(builder.vertices);
    XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.fov={-.7f,.7f,.7f,-.7f};
    graphics.render(0,1,2,view,0);auto initial=readback(graphics,graphics.swapchain(0),1);
    check(std::all_of(initial.begin(),initial.end(),[](uint8_t v){return v==0;}),"new measurements start with transparent reveal");
    graphics.render(0,1,2,view,1);auto filled=readback(graphics,graphics.swapchain(0),1);const auto center=(64*128+64)*4;
    check(filled[center+3]>0&&filled[center+3]<255&&filled[3]==0,"actual shader reveals transparent surfaces while preserving unknown space");
    auto depth=readback(graphics,graphics.depthSwapchain(0),2);float z;std::memcpy(&z,depth.data()+center,4);
    check(std::abs(z-(100.f/99.95f*(1-.05f/2)))<1e-5f,"submitted depth matches the real rendered metric surface");
    graphics.render(0,1,2,view,20,true,20);const auto alignmentReady=readback(graphics,graphics.swapchain(0),1);
    check(std::all_of(alignmentReady.begin(),alignmentReady.end(),[](uint8_t v){return v==0;}),"waiting for measured origin alignment cannot consume the reveal before it becomes visible");
    graphics.render(0,1,2,view,21,true,20);const auto afterAlignment=readback(graphics,graphics.swapchain(0),1);
    check(afterAlignment==filled,"the actual GPU reveal starts when verified geometry becomes visible");
    graphics.render(1,0,1,view,1);auto other=readback(graphics,graphics.swapchain(1),0);check(filled==other,"both eyes render correctly with independent color and depth image indices");
    graphics.render(0,0,0,view,1,false);auto hidden=readback(graphics,graphics.swapchain(0),0);check(std::all_of(hidden.begin(),hidden.end(),[](uint8_t v){return v==0;}),"an unverified origin hides all uploaded geometry");
    const auto triangle=[](float z,std::array<float,4> color){std::vector<room_native::SceneVertex> result;unsigned i=0;
        for(const auto p:{room_native::V3{-.4f*z,-.4f*z,-z},room_native::V3{.4f*z,-.4f*z,-z},room_native::V3{0,.4f*z,-z}}){
            room_native::SceneVertex vertex{{p.x,p.y,p.z},{color[0],color[1],color[2],color[3]},{0,0,0},0,4};vertex.barycentric[i++]=1;result.push_back(vertex);}return result;};
    const auto front=triangle(1,{0,.7f,1,.4f}),rear=triangle(2,{1,0,0,1});auto ordered=front;ordered.insert(ordered.end(),rear.begin(),rear.end());
    graphics.upload(ordered);graphics.render(0,0,0,view,1);const auto nearFirst=readback(graphics,graphics.swapchain(0),0);
    ordered=rear;ordered.insert(ordered.end(),front.begin(),front.end());graphics.upload(ordered);graphics.render(0,0,0,view,1);const auto farFirst=readback(graphics,graphics.swapchain(0),0);
    for(unsigned i=0;i<4;++i)check(nearFirst[center+i]==farFirst[center+i],"a hidden rear surface cannot contribute through a nearer translucent surface in either drawing order");
    check(farFirst[center]==0&&farFirst[center+2]>0,"only the nearest measured surface contributes its color");
    std::vector<room_native::SceneVertex> gaussian;
    for(const auto& uv:std::array<std::array<float,2>,6>{{{-3,-3},{3,-3},{3,3},{-3,-3},{3,3},{-3,3}}})
        gaussian.push_back({{uv[0]*.18f,uv[1]*.18f,-2},{.12f,.88f,.78f,.60f},{uv[0],uv[1],1},0,5});
    graphics.upload(gaussian);graphics.render(0,0,0,view,0);const auto gaussianNew=readback(graphics,graphics.swapchain(0),0);
    graphics.render(0,0,0,view,2);const auto gaussianSettled=readback(graphics,graphics.swapchain(0),0);
    check(gaussianNew[center+3]==0&&gaussianSettled[center+3]>100,"Gaussian surface reveals with animated alpha");
    const auto gaussianCorner=(std::size_t(50)*128+50)*4;
    check(gaussianSettled[gaussianCorner+3]==0,"Gaussian footprint discards unknown corners rather than filling its quad");
    const auto gaussianDepth=readback(graphics,graphics.depthSwapchain(0),0);float cornerDepth=0;std::memcpy(&cornerDepth,gaussianDepth.data()+gaussianCorner,4);
    check(cornerDepth==1,"discarded Gaussian corners do not occlude unobserved space");
    check(gaussianSettled[center]>gaussianSettled[center+2],"uncertain Gaussian surfaces settle to amber guidance");
    room_native::SceneMeshBuilder connected;
    connected.measuredSurface({-.5f,-.5f,-2},{.5f,-.5f,-2},{0,.5f,-2},true,0);
    graphics.upload(connected.vertices);graphics.render(0,0,0,view,0);
    const auto surfaceNew=readback(graphics,graphics.swapchain(0),0);
    graphics.render(0,0,0,view,2);const auto surface=readback(graphics,graphics.swapchain(0),0);
    check(surfaceNew[center+3]==0&&surface[center+3]>0&&surface[3]==0,"measured connected surface animates without filling unknown space");
    check(surface[center+1]>surface[center]&&surface[center+2]>surface[center],"connected surface uses teal and blue instead of large white circles");
    connected.vertices.clear();connected.measuredSurface({-.5f,-.5f,-2},{.5f,-.5f,-2},{0,.5f,-2},false,0);
    graphics.upload(connected.vertices);graphics.render(0,0,0,view,2);
    const auto uncertain=readback(graphics,graphics.swapchain(0),0);
    check(uncertain[center+3]<surface[center+3],"uncertain measured regions stay visibly lighter than confirmed surfaces");
    std::puts("PASS connected measured surface shader, animated teal reveal, confidence and real gaps");
    graphics.shutdown();check(chains.empty(),"all owned swapchain images are released");dlclose(loader);
    std::puts("PASS real native MR shaders, animated alpha, transparent gaps, metric depth, order-independent occlusion and independent per-eye swapchains");
    std::puts("PASS real Gaussian shader, animated reveal, uncertainty colour and transparent colour/depth footprint");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
