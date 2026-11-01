// Executes real Vulkan transfers without OpenVR, a headset, or camera access.
#include "room_scan/native_gpu.h"
#include <cstdio>
using namespace room_native;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::uint32_t extensions(char*,std::uint32_t){return 0;}
std::uint32_t deviceExtensions(VkPhysicalDevice_T*,char*,std::uint32_t){return 0;}
bool missingOutput=false,outputQueried=false;
void outputDevice(std::uint64_t* output,ETextureType type,VkInstance_T* instance){
    outputQueried=true;*output=0;check(type==ETextureType_TextureType_Vulkan,"runtime output GPU uses the Vulkan instance");
    if(missingOutput)return;
    void* loader=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);check(loader!=nullptr,"test Vulkan loader");
    void* address=dlsym(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr get=nullptr;std::memcpy(&get,&address,sizeof(get));
    auto entry=get(instance,"vkEnumeratePhysicalDevices");PFN_vkEnumeratePhysicalDevices enumerate=nullptr;std::memcpy(&enumerate,&entry,sizeof(enumerate));
    unsigned count=0;check(enumerate(instance,&count,nullptr)==VK_SUCCESS&&count>0&&count<=16,"test GPU enumeration");
    std::vector<VkPhysicalDevice> devices(count);check(enumerate(instance,&count,devices.data())==VK_SUCCESS,"test GPU list");
    *output=std::uint64_t(reinterpret_cast<std::uintptr_t>(devices.front()));dlclose(loader);
}
}
int main(){try{
    VR_IVRCompositor_FnTable compositor{};compositor.GetVulkanInstanceExtensionsRequired=extensions;compositor.GetVulkanDeviceExtensionsRequired=deviceExtensions;
    VR_IVRSystem_FnTable system{};system.GetOutputDevice=outputDevice;
    OverlayGpu gpu;gpu.initialize(&system,&compositor);std::array<std::uint64_t,5> images{};
    check(outputQueried,"the actual output-device selection path runs");
    for(unsigned iteration=0;iteration<30;++iteration)for(unsigned index=0;index<5;++index){
        Canvas canvas(index==0?1024:index==1?128:512,index==0?800:index==1?128:512);
        for(std::size_t byte=0;byte<canvas.pixels.size();++byte)canvas.pixels[byte]=std::uint8_t(byte*13+index*41+iteration*7);
        const auto result=gpu.present(index,canvas,[&](Texture_t* texture){
            check(texture->eType==ETextureType_TextureType_Vulkan,"only GPU textures are submitted");
            const auto& d=*static_cast<VRVulkanTextureData_t*>(texture->handle);
            check(d.m_nWidth==unsigned(canvas.width)&&d.m_nHeight==unsigned(canvas.height)&&d.m_nFormat==VK_FORMAT_R8G8B8A8_UNORM,"exact texture descriptor");
            if(!iteration)images[index]=d.m_nImage;else check(images[index]==d.m_nImage,"hover and animation reuse their persistent image");
            return 0;
        });
        check(result==0&&gpu.readback(index)==canvas.pixels,"actual GPU upload/readback preserves every colour, alpha and row byte");
    }
    Canvas changed(2,2);bool refused=false;
    try{gpu.present(0,changed,[](Texture_t*){return 0;});}catch(const std::runtime_error&){refused=true;}
    check(refused,"unexpected extent cannot replace a live texture");
    missingOutput=true;refused=false;
    try{OverlayGpu wrongGpu;wrongGpu.initialize(&system,&compositor);}catch(const std::runtime_error&){refused=true;}
    check(refused,"an absent runtime GPU never silently selects another device");
    std::puts("PASS real Vulkan uploads/readback, 150 changing frames, stable image allocation and exact RGBA");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
