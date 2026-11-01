// Persistent offscreen Vulkan textures. Only the overlay thread uses this queue.
// OpenVR copies from TRANSFER_SRC_OPTIMAL on our queue; a fence after that call
// retires its copy before we reuse either the image or the mapped staging bytes.
#pragma once
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <openvr_capi.h>
#include "native_raster.h"
#include <dlfcn.h>
#include <sstream>
#include <cstring>

namespace room_native {
class OverlayGpu {
    struct Slot {
        VkImage image=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE,stagingMemory=VK_NULL_HANDLE;
        VkBuffer staging=VK_NULL_HANDLE;void* mapped=nullptr;unsigned width=0,height=0;bool initialized=false;
        VRVulkanTextureData_t description{};
    };
    void* loader_=nullptr;VkInstance instance_=VK_NULL_HANDLE;VkPhysicalDevice physical_=VK_NULL_HANDLE;
    VkDevice device_=VK_NULL_HANDLE;VkQueue queue_=VK_NULL_HANDLE;unsigned family_=0;
    VkCommandPool pool_=VK_NULL_HANDLE;VkCommandBuffer command_=VK_NULL_HANDLE;VkFence fence_=VK_NULL_HANDLE;
    std::array<Slot,5> slots_{};VkPhysicalDeviceMemoryProperties memory_{};
    PFN_vkGetInstanceProcAddr get_=nullptr;PFN_vkGetDeviceProcAddr getDevice_=nullptr;
#define ROOM_GPU_FUNCTIONS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(CreateCommandPool) X(DestroyCommandPool) \
    X(AllocateCommandBuffers) X(ResetCommandPool) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CreateFence) X(DestroyFence) X(ResetFences) X(WaitForFences) X(QueueSubmit) X(DeviceWaitIdle) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
    X(BindImageMemory) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(MapMemory) X(UnmapMemory) X(CmdPipelineBarrier) X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer)
#define ROOM_GPU_DECLARE(name) PFN_vk##name name##_=nullptr;
    ROOM_GPU_FUNCTIONS(ROOM_GPU_DECLARE)
#undef ROOM_GPU_DECLARE
    PFN_vkDestroyInstance destroyInstance_=nullptr;
    static void checked(VkResult result,const char* operation){
        if(result!=VK_SUCCESS)throw std::runtime_error(std::string("Room Vulkan ")+operation+" code="+std::to_string(int(result)));
    }
    template<class T>T instanceFunction(const char* name){
        auto address=get_(instance_,name);if(!address)throw std::runtime_error("Room Vulkan entry unavailable");
        T result;static_assert(sizeof(result)==sizeof(address));std::memcpy(&result,&address,sizeof(result));return result;
    }
    template<class Query>static std::vector<std::string> extensions(Query query){
        const auto size=query(nullptr,0);if(!size)return {};
        if(size>16384)throw std::runtime_error("Room Vulkan extension list exceeds budget");
        std::vector<char> text(size);if(query(text.data(),size)!=size||text.back())throw std::runtime_error("Room Vulkan extension list changed");
        std::istringstream input(text.data());std::vector<std::string> out;std::string token;
        while(input>>token){if(token.size()>255||out.size()>=128)throw std::runtime_error("Invalid Room Vulkan extensions");out.push_back(token);}return out;
    }
    unsigned memoryType(unsigned bits,VkMemoryPropertyFlags flags){
        for(unsigned i=0;i<memory_.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory_.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("Room Vulkan compatible memory unavailable");
    }
    void allocate(VkMemoryRequirements requirements,VkMemoryPropertyFlags flags,VkDeviceMemory& memory){
        if(requirements.size>32u*1024*1024)throw std::runtime_error("Room Vulkan allocation exceeds budget");
        VkMemoryAllocateInfo info{};info.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;info.allocationSize=requirements.size;
        info.memoryTypeIndex=memoryType(requirements.memoryTypeBits,flags);checked(AllocateMemory_(device_,&info,nullptr,&memory),"allocate");
    }
    void begin(){
        checked(ResetCommandPool_(device_,pool_,0),"reset commands");VkCommandBufferBeginInfo info{};
        info.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;info.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(BeginCommandBuffer_(command_,&info),"begin commands");
    }
    void submit(bool command){
        if(command)checked(EndCommandBuffer_(command_),"end commands");
        checked(ResetFences_(device_,1,&fence_),"reset fence");VkSubmitInfo info{};info.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if(command){info.commandBufferCount=1;info.pCommandBuffers=&command_;}
        checked(QueueSubmit_(queue_,1,&info,fence_),"submit");
        checked(WaitForFences_(device_,1,&fence_,VK_TRUE,2000000000ULL),"wait fence");
    }
    void barrier(Slot& slot,VkImageLayout oldLayout,VkImageLayout newLayout,VkAccessFlags source,VkAccessFlags destination){
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.oldLayout=oldLayout;b.newLayout=newLayout;
        b.srcAccessMask=source;b.dstAccessMask=destination;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=slot.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        CmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void create(Slot& slot,unsigned width,unsigned height){
        slot.width=width;slot.height=height;
        VkImageCreateInfo image{};image.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;image.imageType=VK_IMAGE_TYPE_2D;
        image.format=VK_FORMAT_R8G8B8A8_UNORM;image.extent={width,height,1};image.mipLevels=image.arrayLayers=1;
        image.samples=VK_SAMPLE_COUNT_1_BIT;image.tiling=VK_IMAGE_TILING_OPTIMAL;
        image.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;image.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        checked(CreateImage_(device_,&image,nullptr,&slot.image),"create image");VkMemoryRequirements requirements{};
        GetImageMemoryRequirements_(device_,slot.image,&requirements);allocate(requirements,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,slot.memory);
        checked(BindImageMemory_(device_,slot.image,slot.memory,0),"bind image");
        VkBufferCreateInfo buffer{};buffer.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;buffer.size=std::uint64_t(width)*height*4;
        buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        checked(CreateBuffer_(device_,&buffer,nullptr,&slot.staging),"create staging");GetBufferMemoryRequirements_(device_,slot.staging,&requirements);
        allocate(requirements,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,slot.stagingMemory);
        checked(BindBufferMemory_(device_,slot.staging,slot.stagingMemory,0),"bind staging");
        checked(MapMemory_(device_,slot.stagingMemory,0,VK_WHOLE_SIZE,0,&slot.mapped),"map staging");
        slot.description={std::uint64_t(reinterpret_cast<std::uintptr_t>(slot.image)),device_,physical_,instance_,queue_,family_,width,height,VK_FORMAT_R8G8B8A8_UNORM,1};
    }
public:
    OverlayGpu()=default;OverlayGpu(const OverlayGpu&)=delete;OverlayGpu& operator=(const OverlayGpu&)=delete;
    void initialize(VR_IVRSystem_FnTable* system,VR_IVRCompositor_FnTable* compositor){
        if(loader_||!system||!system->GetOutputDevice||!compositor||!compositor->GetVulkanInstanceExtensionsRequired||!compositor->GetVulkanDeviceExtensionsRequired)
            throw std::runtime_error("Room Vulkan runtime interface unavailable");
        loader_=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);if(!loader_)throw std::runtime_error("Room Vulkan loader unavailable");
        auto address=dlsym(loader_,"vkGetInstanceProcAddr");std::memcpy(&get_,&address,sizeof(get_));if(!get_)throw std::runtime_error("Room Vulkan loader entry unavailable");
        const auto instanceExtensions=extensions([&](char* p,unsigned n){return compositor->GetVulkanInstanceExtensionsRequired(p,n);});
        std::vector<const char*> names;for(const auto& name:instanceExtensions)names.push_back(name.c_str());
        VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.pApplicationName="Room Setup";app.apiVersion=VK_API_VERSION_1_0;
        VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
        info.enabledExtensionCount=unsigned(names.size());info.ppEnabledExtensionNames=names.data();
        checked(instanceFunction<PFN_vkCreateInstance>("vkCreateInstance")(&info,nullptr,&instance_),"create instance");
        destroyInstance_=instanceFunction<PFN_vkDestroyInstance>("vkDestroyInstance");
        auto enumerate=instanceFunction<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");unsigned count=0;
        checked(enumerate(instance_,&count,nullptr),"enumerate devices");if(!count||count>16)throw std::runtime_error("Room Vulkan device count invalid");
        std::vector<VkPhysicalDevice> devices(count);checked(enumerate(instance_,&count,devices.data()),"enumerate devices");
        std::uint64_t output=0;system->GetOutputDevice(&output,ETextureType_TextureType_Vulkan,instance_);
        for(auto device:devices)if(std::uint64_t(reinterpret_cast<std::uintptr_t>(device))==output)physical_=device;
        if(!physical_)throw std::runtime_error("Room Vulkan headset GPU unavailable");
        auto queueProperties=instanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        count=0;queueProperties(physical_,&count,nullptr);if(!count||count>128)throw std::runtime_error("Room Vulkan queue count invalid");
        std::vector<VkQueueFamilyProperties> queues(count);queueProperties(physical_,&count,queues.data());family_=count;
        for(unsigned i=0;i<count;++i)if(queues[i].queueCount&&(queues[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){family_=i;break;}
        if(family_==count)throw std::runtime_error("Room Vulkan graphics queue unavailable");
        instanceFunction<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(physical_,&memory_);
        const auto deviceExtensions=extensions([&](char* p,unsigned n){return compositor->GetVulkanDeviceExtensionsRequired(physical_,p,n);});
        names.clear();for(const auto& name:deviceExtensions)names.push_back(name.c_str());
        const float priority=1;VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex=family_;queue.queueCount=1;queue.pQueuePriorities=&priority;
        VkDeviceCreateInfo deviceInfo{};deviceInfo.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queue;
        deviceInfo.enabledExtensionCount=unsigned(names.size());deviceInfo.ppEnabledExtensionNames=names.data();
        checked(instanceFunction<PFN_vkCreateDevice>("vkCreateDevice")(physical_,&deviceInfo,nullptr,&device_),"create device");
        getDevice_=instanceFunction<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
#define ROOM_GPU_LOAD(name) {auto fn=getDevice_(device_,"vk" #name);std::memcpy(&name##_,&fn,sizeof(fn));if(!name##_)throw std::runtime_error("Room Vulkan device entry unavailable");}
        ROOM_GPU_FUNCTIONS(ROOM_GPU_LOAD)
#undef ROOM_GPU_LOAD
        GetDeviceQueue_(device_,family_,0,&queue_);VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=family_;
        checked(CreateCommandPool_(device_,&pool,nullptr,&pool_),"create commands");VkCommandBufferAllocateInfo command{};
        command.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;command.commandPool=pool_;command.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command.commandBufferCount=1;
        checked(AllocateCommandBuffers_(device_,&command,&command_),"allocate commands");VkFenceCreateInfo fence{};fence.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        checked(CreateFence_(device_,&fence,nullptr,&fence_),"create fence");
    }
    template<class Submit>int present(unsigned index,const Canvas& canvas,Submit consumer){
        if(!device_||canvas.width<=0||canvas.height<=0||canvas.width>1024||canvas.height>1024
            ||canvas.pixels.size()!=std::size_t(canvas.width)*canvas.height*4)throw std::runtime_error("Room GPU image extent invalid");
        auto& slot=slots_.at(index);if(!slot.image)create(slot,unsigned(canvas.width),unsigned(canvas.height));
        if(slot.width!=unsigned(canvas.width)||slot.height!=unsigned(canvas.height))throw std::runtime_error("Room GPU image extent changed");
        std::memcpy(slot.mapped,canvas.pixels.data(),canvas.pixels.size());begin();
        barrier(slot,slot.initialized?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            slot.initialized?VK_ACCESS_TRANSFER_READ_BIT:0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={slot.width,slot.height,1};
        CmdCopyBufferToImage_(command_,slot.staging,slot.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier(slot,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        submit(true);slot.initialized=true;
        Texture_t texture{&slot.description,ETextureType_TextureType_Vulkan,EColorSpace_ColorSpace_Gamma};
        const int error=int(consumer(&texture));submit(false);return error;
    }
    // Read back the real GPU image for offscreen transfer/alpha/stride tests.
    std::vector<std::uint8_t> readback(unsigned index){
        auto& slot=slots_.at(index);if(!slot.initialized)throw std::runtime_error("Room GPU image unavailable");begin();
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={slot.width,slot.height,1};
        CmdCopyImageToBuffer_(command_,slot.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,slot.staging,1,&copy);
        VkMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        CmdPipelineBarrier_(command_,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);submit(true);
        const auto* bytes=static_cast<const std::uint8_t*>(slot.mapped);return {bytes,bytes+std::size_t(slot.width)*slot.height*4};
    }
    ~OverlayGpu(){
        // Runtime's destructor shuts OpenVR down first, as required by its Vulkan contract.
        if(device_&&DeviceWaitIdle_)DeviceWaitIdle_(device_);
        if(device_){for(auto& slot:slots_){
            if(slot.mapped)UnmapMemory_(device_,slot.stagingMemory);
            if(slot.staging)DestroyBuffer_(device_,slot.staging,nullptr);
            if(slot.stagingMemory)FreeMemory_(device_,slot.stagingMemory,nullptr);
            if(slot.image)DestroyImage_(device_,slot.image,nullptr);
            if(slot.memory)FreeMemory_(device_,slot.memory,nullptr);
        }
            if(fence_)DestroyFence_(device_,fence_,nullptr);
            if(pool_)DestroyCommandPool_(device_,pool_,nullptr);
            if(DestroyDevice_)DestroyDevice_(device_,nullptr);
        }
        if(instance_&&destroyInstance_)destroyInstance_(instance_,nullptr);
        if(loader_)dlclose(loader_);
    }
};
#undef ROOM_GPU_FUNCTIONS
} // namespace room_native
