// OpenXR API layer: Steam Frame passthrough through the standard OpenXR paths
// (XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND and XR_FB_passthrough) for any
// OpenXR application on SteamVR. It adds only what the runtime lacks, only
// while a trusted sender answers the application source pipe, and declares the
// application's ordinary RGBA source over SBN1 while a frame really carries
// coverage alpha. Pixels are never changed or derived from colour.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include "openxr/openxr_frame_policy.h"
#include "openxr/openxr_source_windows.h"
#include "bridge/stream_bridge_log.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace stream_bridge::openxr;
using stream_bridge::logging::Level;
constexpr const char* LayerName="XR_APILAYER_SFNP_passthrough";
void report(Level level,const char* text){
    if(!stream_bridge::logging::enabled(level))return;
    char line[512];std::snprintf(line,sizeof line,"[%s] %s\n",LayerName,text);OutputDebugStringA(line);
}
// A sender that answered once is kept for the process; an absent one is asked
// again at most every 2 s, bounded to 500 ms on the calling (non-frame) thread.
struct Capability {
    std::atomic<int> answered{0};std::atomic<ULONGLONG> next{0};
    bool present(){
        if(answered.load(std::memory_order_acquire)>0)return true;
        const auto now=GetTickCount64();if(now<next.load(std::memory_order_acquire))return false;
        next.store(now+2000,std::memory_order_release);
        if(!probeSource(500))return false;
        answered.store(1,std::memory_order_release);report(Level::Important,"Steam Link sender answered; ALPHA_BLEND and XR_FB_passthrough are offered");return true;
    }
} capability;
struct Passthrough {XrSession session=XR_NULL_HANDLE;bool running=false;};
struct PassthroughLayer {Passthrough* owner=nullptr;bool running=false;float opacity=1;};
struct Session {
    XrInstance instance=XR_NULL_HANDLE;Graphics api=Graphics::Unknown;std::unique_ptr<SourceClient> source=std::make_unique<SourceClient>();
    FrameRefusal refusal=FrameRefusal::NotRequested;bool reportedGrant=false;std::uint64_t generation=0;
};
struct Instance {
    PFN_xrGetInstanceProcAddr next=nullptr;bool fb=false,alphaBlend=false;
    PFN_xrDestroyInstance destroyInstance=nullptr;PFN_xrGetSystemProperties systemProperties=nullptr;
    PFN_xrEnumerateEnvironmentBlendModes blendModes=nullptr;PFN_xrCreateSession createSession=nullptr;
    PFN_xrDestroySession destroySession=nullptr;PFN_xrEndSession endSession=nullptr;PFN_xrCreateSwapchain createSwapchain=nullptr;
    PFN_xrDestroySwapchain destroySwapchain=nullptr;PFN_xrEndFrame endFrame=nullptr;
};
std::mutex mutex;
std::map<XrInstance,Instance> instances;
std::map<XrSession,Session> sessions;
struct Swapchain {SwapchainShape shape;XrInstance instance=XR_NULL_HANDLE;};
std::map<XrSwapchain,Swapchain> swapchains;
std::set<Passthrough*> passthroughs;std::set<PassthroughLayer*> passthroughLayers;
template<class T> void resolve(const Instance& owner,XrInstance instance,const char* name,T& out){
    PFN_xrVoidFunction function=nullptr;if(owner.next(instance,name,&function)==XR_SUCCESS)out=reinterpret_cast<T>(function);
}
Instance* instanceOf(XrInstance instance){auto it=instances.find(instance);return it==instances.end()?nullptr:&it->second;}
Instance* instanceOfSession(XrSession session){auto it=sessions.find(session);return it==sessions.end()?nullptr:instanceOf(it->second.instance);}

XRAPI_ATTR XrResult XRAPI_CALL getInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* function);

XRAPI_ATTR XrResult XRAPI_CALL destroyInstance(XrInstance instance){
    PFN_xrDestroyInstance next=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOf(instance);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->destroyInstance;
     for(auto it=sessions.begin();it!=sessions.end();)it=it->second.instance==instance?sessions.erase(it):std::next(it);instances.erase(instance);}
    return next?next(instance):XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL getSystemProperties(XrInstance instance,XrSystemId system,XrSystemProperties* properties){
    PFN_xrGetSystemProperties next=nullptr;bool fb=false;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOf(instance);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->systemProperties;fb=owner->fb;}
    const auto result=next(instance,system,properties);
    if(result!=XR_SUCCESS||!fb)return result;
    const bool present=capability.present();
    for(auto* chain=static_cast<XrBaseOutStructure*>(properties->next);chain;chain=chain->next){
        if(chain->type==XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB)reinterpret_cast<XrSystemPassthroughPropertiesFB*>(chain)->supportsPassthrough=present;
        if(chain->type==XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB)reinterpret_cast<XrSystemPassthroughProperties2FB*>(chain)->capabilities=present?XR_PASSTHROUGH_CAPABILITY_BIT_FB:0;
    }
    return result;
}
XRAPI_ATTR XrResult XRAPI_CALL enumerateEnvironmentBlendModes(XrInstance instance,XrSystemId system,XrViewConfigurationType view,
    uint32_t capacity,uint32_t* count,XrEnvironmentBlendMode* modes){
    PFN_xrEnumerateEnvironmentBlendModes next=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOf(instance);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->blendModes;}
    if(view!=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO)return next(instance,system,view,capacity,count,modes);
    uint32_t total=0;auto result=next(instance,system,view,0,&total,nullptr);if(result!=XR_SUCCESS)return result;
    std::vector<XrEnvironmentBlendMode> runtime(total);
    if(total&&(result=next(instance,system,view,total,&total,runtime.data()))!=XR_SUCCESS)return result;
    runtime.resize(total);
    bool native=false;for(auto mode:runtime)native|=mode==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
    if(native||!capability.present())return next(instance,system,view,capacity,count,modes);
    {std::lock_guard<std::mutex> guard(mutex);if(auto* owner=instanceOf(instance))owner->alphaBlend=true;}
    return withAlphaBlend(runtime,capacity,count,modes);
}
XRAPI_ATTR XrResult XRAPI_CALL createSession(XrInstance instance,const XrSessionCreateInfo* info,XrSession* session){
    PFN_xrCreateSession next=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOf(instance);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->createSession;}
    const auto result=next(instance,info,session);if(result!=XR_SUCCESS)return result;
    Graphics api=Graphics::Unknown;
    for(auto* chain=info?static_cast<const XrBaseInStructure*>(info->next):nullptr;chain;chain=chain->next){
        if(chain->type==XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)api=Graphics::D3D11;
        else if(chain->type==XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)api=Graphics::D3D12;
        else if(chain->type==XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR)api=Graphics::Vulkan;
        else if(chain->type==XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR)api=Graphics::OpenGL;
    }
    try{std::lock_guard<std::mutex> guard(mutex);auto& state=sessions[*session];state.instance=instance;state.api=api;}catch(...){}
    return result;
}
XRAPI_ATTR XrResult XRAPI_CALL destroySession(XrSession session){
    PFN_xrDestroySession next=nullptr;std::unique_ptr<SourceClient> source;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOfSession(session);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->destroySession;
     source=std::move(sessions[session].source);sessions.erase(session);
     for(auto it=passthroughLayers.begin();it!=passthroughLayers.end();){if((*it)->owner->session==session){delete *it;it=passthroughLayers.erase(it);}else ++it;}
     for(auto it=passthroughs.begin();it!=passthroughs.end();){if((*it)->session==session){delete *it;it=passthroughs.erase(it);}else ++it;}}
    source.reset(); // Closes the declaration before the session's images go away.
    return next(session);
}
XRAPI_ATTR XrResult XRAPI_CALL endSession(XrSession session){
    PFN_xrEndSession next=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOfSession(session);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->endSession;sessions[session].source->want(false);}
    return next(session);
}
XRAPI_ATTR XrResult XRAPI_CALL createSwapchain(XrSession session,const XrSwapchainCreateInfo* info,XrSwapchain* swapchain){
    PFN_xrCreateSwapchain next=nullptr;
    XrInstance instance=XR_NULL_HANDLE;
    {std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOfSession(session);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->createSwapchain;instance=sessions[session].instance;}
    const auto result=next(session,info,swapchain);
    if(result==XR_SUCCESS&&info)try{std::lock_guard<std::mutex> guard(mutex);swapchains[*swapchain]={{info->format,info->arraySize,info->mipCount,info->sampleCount},instance};}catch(...){}
    return result;
}
XRAPI_ATTR XrResult XRAPI_CALL destroySwapchain(XrSwapchain swapchain){
    PFN_xrDestroySwapchain next=nullptr;
    {std::lock_guard<std::mutex> guard(mutex);auto it=swapchains.find(swapchain);if(it==swapchains.end())return XR_ERROR_HANDLE_INVALID;
     if(auto* owner=instanceOf(it->second.instance))next=owner->destroySwapchain;swapchains.erase(it);}
    return next?next(swapchain):XR_ERROR_HANDLE_INVALID;
}
XRAPI_ATTR XrResult XRAPI_CALL endFrame(XrSession session,const XrFrameEndInfo* info){
    if(!info)return XR_ERROR_VALIDATION_FAILURE;
    PFN_xrEndFrame next=nullptr;FramePlan plan;bool untouched=false;
    {
        std::lock_guard<std::mutex> guard(mutex);auto* owner=instanceOfSession(session);if(!owner)return XR_ERROR_HANDLE_INVALID;next=owner->endFrame;
        untouched=!owner->fb&&!owner->alphaBlend;
    }
    if(untouched)return next(session,info);
    {
        std::lock_guard<std::mutex> guard(mutex);auto it=sessions.find(session);if(it==sessions.end())return XR_ERROR_HANDLE_INVALID;
        auto& state=it->second;auto* owner=instanceOf(state.instance);if(!owner)return XR_ERROR_HANDLE_INVALID;
        try{
            plan=planFrame(*info,owner->alphaBlend,owner->fb,state.api,
                [](XrPassthroughLayerFB handle){
                    PassthroughLayerState out;auto* layer=reinterpret_cast<PassthroughLayer*>(handle);
                    if(passthroughLayers.count(layer)){out.known=true;out.running=layer->running&&layer->owner->running;out.opacity=layer->opacity;}
                    return out;
                },
                [](XrSwapchain handle,SwapchainShape& shape){auto it=swapchains.find(handle);if(it==swapchains.end())return false;shape=it->second.shape;return true;});
        }catch(...){return XR_ERROR_RUNTIME_FAILURE;}
        // Declare only frames that carry coverage; withdrawal is immediate.
        state.source->want(plan.coverage);
        if(plan.refusal!=state.refusal){
            state.refusal=plan.refusal;
            if(plan.requested&&!plan.coverage){std::string text=std::string("passthrough requested but not declared: ")+refusalName(plan.refusal);report(Level::Warning,text.c_str());}
        }
        const auto current=state.source->state();
        if(current.granted!=state.reportedGrant||current.generation!=state.generation){
            state.reportedGrant=current.granted;state.generation=current.generation;
            report(Level::Important,current.granted?"sender admitted this application's RGBA source":"sender has not admitted this application's RGBA source; ordinary opaque video");
        }
    }
    XrFrameEndInfo forwarded=*info;forwarded.environmentBlendMode=plan.mode;
    forwarded.layerCount=uint32_t(plan.layers.size());forwarded.layers=plan.layers.empty()?nullptr:plan.layers.data();
    return next(session,&forwarded);
}

// XR_FB_passthrough, provided only when the runtime lacks it.
XRAPI_ATTR XrResult XRAPI_CALL createPassthrough(XrSession session,const XrPassthroughCreateInfoFB* info,XrPassthroughFB* out){
    if(!info||!out||info->type!=XR_TYPE_PASSTHROUGH_CREATE_INFO_FB)return XR_ERROR_VALIDATION_FAILURE;
    {std::lock_guard<std::mutex> guard(mutex);if(!sessions.count(session))return XR_ERROR_HANDLE_INVALID;}
    if(!capability.present())return XR_ERROR_FEATURE_UNSUPPORTED;
    if(info->flags&XR_PASSTHROUGH_LAYER_DEPTH_BIT_FB)return XR_ERROR_FEATURE_UNSUPPORTED;
    try{
        std::lock_guard<std::mutex> guard(mutex);auto* passthrough=new Passthrough{session,(info->flags&XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB)!=0};
        passthroughs.insert(passthrough);*out=reinterpret_cast<XrPassthroughFB>(passthrough);return XR_SUCCESS;
    }catch(...){return XR_ERROR_OUT_OF_MEMORY;}
}
Passthrough* passthroughOf(XrPassthroughFB handle){auto* p=reinterpret_cast<Passthrough*>(handle);return passthroughs.count(p)?p:nullptr;}
PassthroughLayer* layerOf(XrPassthroughLayerFB handle){auto* p=reinterpret_cast<PassthroughLayer*>(handle);return passthroughLayers.count(p)?p:nullptr;}
XRAPI_ATTR XrResult XRAPI_CALL destroyPassthrough(XrPassthroughFB handle){
    std::lock_guard<std::mutex> guard(mutex);auto* passthrough=passthroughOf(handle);if(!passthrough)return XR_ERROR_HANDLE_INVALID;
    for(auto it=passthroughLayers.begin();it!=passthroughLayers.end();){if((*it)->owner==passthrough){delete *it;it=passthroughLayers.erase(it);}else ++it;}
    passthroughs.erase(passthrough);delete passthrough;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL passthroughStart(XrPassthroughFB handle){std::lock_guard<std::mutex> guard(mutex);auto* p=passthroughOf(handle);if(!p)return XR_ERROR_HANDLE_INVALID;p->running=true;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL passthroughPause(XrPassthroughFB handle){std::lock_guard<std::mutex> guard(mutex);auto* p=passthroughOf(handle);if(!p)return XR_ERROR_HANDLE_INVALID;p->running=false;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL createPassthroughLayer(XrSession session,const XrPassthroughLayerCreateInfoFB* info,XrPassthroughLayerFB* out){
    if(!info||!out||info->type!=XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB)return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> guard(mutex);if(!sessions.count(session))return XR_ERROR_HANDLE_INVALID;
    auto* passthrough=passthroughOf(info->passthrough);if(!passthrough||passthrough->session!=session)return XR_ERROR_HANDLE_INVALID;
    // The Frame shows its camera image as-is: reconstruction only, no projected surfaces or depth.
    if(info->purpose!=XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB||(info->flags&XR_PASSTHROUGH_LAYER_DEPTH_BIT_FB))return XR_ERROR_FEATURE_UNSUPPORTED;
    try{
        auto* layer=new PassthroughLayer{passthrough,(info->flags&XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB)!=0,1};
        passthroughLayers.insert(layer);*out=reinterpret_cast<XrPassthroughLayerFB>(layer);return XR_SUCCESS;
    }catch(...){return XR_ERROR_OUT_OF_MEMORY;}
}
XRAPI_ATTR XrResult XRAPI_CALL destroyPassthroughLayer(XrPassthroughLayerFB handle){
    std::lock_guard<std::mutex> guard(mutex);auto* layer=layerOf(handle);if(!layer)return XR_ERROR_HANDLE_INVALID;passthroughLayers.erase(layer);delete layer;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL passthroughLayerPause(XrPassthroughLayerFB handle){std::lock_guard<std::mutex> guard(mutex);auto* l=layerOf(handle);if(!l)return XR_ERROR_HANDLE_INVALID;l->running=false;return XR_SUCCESS;}
XRAPI_ATTR XrResult XRAPI_CALL passthroughLayerResume(XrPassthroughLayerFB handle){std::lock_guard<std::mutex> guard(mutex);auto* l=layerOf(handle);if(!l)return XR_ERROR_HANDLE_INVALID;l->running=true;return XR_SUCCESS;}
// Opacity only: 0 hides the camera; any other value shows it fully, since the
// Frame composes E+(1-A)*camera without a separate camera weight. Colour maps
// and edges are not applied.
XRAPI_ATTR XrResult XRAPI_CALL passthroughLayerSetStyle(XrPassthroughLayerFB handle,const XrPassthroughStyleFB* style){
    if(!style||style->type!=XR_TYPE_PASSTHROUGH_STYLE_FB)return XR_ERROR_VALIDATION_FAILURE;
    std::lock_guard<std::mutex> guard(mutex);auto* l=layerOf(handle);if(!l)return XR_ERROR_HANDLE_INVALID;
    l->opacity=style->textureOpacityFactor<0?0:style->textureOpacityFactor>1?1:style->textureOpacityFactor;return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL createGeometryInstance(XrSession,const XrGeometryInstanceCreateInfoFB*,XrGeometryInstanceFB*){return XR_ERROR_FEATURE_UNSUPPORTED;}
XRAPI_ATTR XrResult XRAPI_CALL destroyGeometryInstance(XrGeometryInstanceFB){return XR_ERROR_HANDLE_INVALID;}
XRAPI_ATTR XrResult XRAPI_CALL geometryInstanceSetTransform(XrGeometryInstanceFB,const XrGeometryInstanceTransformFB*){return XR_ERROR_HANDLE_INVALID;}

XRAPI_ATTR XrResult XRAPI_CALL getInstanceProcAddr(XrInstance instance,const char* name,PFN_xrVoidFunction* function){
    if(!name||!function)return XR_ERROR_VALIDATION_FAILURE;
    PFN_xrGetInstanceProcAddr next=nullptr;bool fb=false;
    {std::lock_guard<std::mutex> guard(mutex);if(auto* owner=instanceOf(instance)){next=owner->next;fb=owner->fb;}}
    if(!next){*function=nullptr;return instance==XR_NULL_HANDLE?XR_ERROR_FUNCTION_UNSUPPORTED:XR_ERROR_HANDLE_INVALID;}
    const std::string text(name);
#define SFNP_HOOK(api,local) if(text==api){*function=reinterpret_cast<PFN_xrVoidFunction>(local);return XR_SUCCESS;}
    SFNP_HOOK("xrGetInstanceProcAddr",getInstanceProcAddr)SFNP_HOOK("xrDestroyInstance",destroyInstance)
    SFNP_HOOK("xrGetSystemProperties",getSystemProperties)SFNP_HOOK("xrEnumerateEnvironmentBlendModes",enumerateEnvironmentBlendModes)
    SFNP_HOOK("xrCreateSession",createSession)SFNP_HOOK("xrDestroySession",destroySession)SFNP_HOOK("xrEndSession",endSession)
    SFNP_HOOK("xrCreateSwapchain",createSwapchain)SFNP_HOOK("xrDestroySwapchain",destroySwapchain)SFNP_HOOK("xrEndFrame",endFrame)
    if(fb){
        SFNP_HOOK("xrCreatePassthroughFB",createPassthrough)SFNP_HOOK("xrDestroyPassthroughFB",destroyPassthrough)
        SFNP_HOOK("xrPassthroughStartFB",passthroughStart)SFNP_HOOK("xrPassthroughPauseFB",passthroughPause)
        SFNP_HOOK("xrCreatePassthroughLayerFB",createPassthroughLayer)SFNP_HOOK("xrDestroyPassthroughLayerFB",destroyPassthroughLayer)
        SFNP_HOOK("xrPassthroughLayerPauseFB",passthroughLayerPause)SFNP_HOOK("xrPassthroughLayerResumeFB",passthroughLayerResume)
        SFNP_HOOK("xrPassthroughLayerSetStyleFB",passthroughLayerSetStyle)SFNP_HOOK("xrCreateGeometryInstanceFB",createGeometryInstance)
        SFNP_HOOK("xrDestroyGeometryInstanceFB",destroyGeometryInstance)SFNP_HOOK("xrGeometryInstanceSetTransformFB",geometryInstanceSetTransform)
    }
#undef SFNP_HOOK
    return next(instance,name,function);
}
bool runtimeHasExtension(PFN_xrGetInstanceProcAddr next,const char* extension){
    PFN_xrVoidFunction raw=nullptr;
    if(next(XR_NULL_HANDLE,"xrEnumerateInstanceExtensionProperties",&raw)!=XR_SUCCESS||!raw)return false;
    const auto enumerate=reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(raw);
    uint32_t count=0;if(enumerate(nullptr,0,&count,nullptr)!=XR_SUCCESS)return false;
    std::vector<XrExtensionProperties> properties(count,{XR_TYPE_EXTENSION_PROPERTIES,nullptr,{},0});
    if(count&&enumerate(nullptr,count,&count,properties.data())!=XR_SUCCESS)return false;
    for(uint32_t i=0;i<count&&i<properties.size();++i)if(!std::strcmp(properties[i].extensionName,extension))return true;
    return false;
}
XRAPI_ATTR XrResult XRAPI_CALL createApiLayerInstance(const XrInstanceCreateInfo* info,const XrApiLayerCreateInfo* layerInfo,XrInstance* instance){
    if(!info||!layerInfo||!layerInfo->nextInfo||!instance)return XR_ERROR_INITIALIZATION_FAILED;
    const auto* nextInfo=layerInfo->nextInfo;
    if(std::strcmp(nextInfo->layerName,LayerName)||!nextInfo->nextGetInstanceProcAddr||!nextInfo->nextCreateApiLayerInstance)return XR_ERROR_INITIALIZATION_FAILED;
    // The manifest advertises XR_FB_passthrough; the runtime never sees it unless it has its own.
    bool requested=false;std::vector<const char*> extensions;
    for(uint32_t i=0;i<info->enabledExtensionCount;++i){
        if(!std::strcmp(info->enabledExtensionNames[i],XR_FB_PASSTHROUGH_EXTENSION_NAME)){requested=true;continue;}
        extensions.push_back(info->enabledExtensionNames[i]);
    }
    const bool provide=requested&&!runtimeHasExtension(nextInfo->nextGetInstanceProcAddr,XR_FB_PASSTHROUGH_EXTENSION_NAME);
    if(requested&&!provide)extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    XrInstanceCreateInfo forwarded=*info;forwarded.enabledExtensionCount=uint32_t(extensions.size());forwarded.enabledExtensionNames=extensions.empty()?nullptr:extensions.data();
    XrApiLayerCreateInfo nextLayer=*layerInfo;nextLayer.nextInfo=nextInfo->next;
    const auto result=nextInfo->nextCreateApiLayerInstance(&forwarded,&nextLayer,instance);
    if(result!=XR_SUCCESS)return result;
    try{
        std::lock_guard<std::mutex> guard(mutex);Instance owner;owner.next=nextInfo->nextGetInstanceProcAddr;owner.fb=provide;
        resolve(owner,*instance,"xrDestroyInstance",owner.destroyInstance);resolve(owner,*instance,"xrGetSystemProperties",owner.systemProperties);
        resolve(owner,*instance,"xrEnumerateEnvironmentBlendModes",owner.blendModes);resolve(owner,*instance,"xrCreateSession",owner.createSession);
        resolve(owner,*instance,"xrDestroySession",owner.destroySession);resolve(owner,*instance,"xrEndSession",owner.endSession);
        resolve(owner,*instance,"xrCreateSwapchain",owner.createSwapchain);resolve(owner,*instance,"xrDestroySwapchain",owner.destroySwapchain);
        resolve(owner,*instance,"xrEndFrame",owner.endFrame);
        if(!owner.destroyInstance||!owner.systemProperties||!owner.blendModes||!owner.createSession||!owner.destroySession||
           !owner.endSession||!owner.createSwapchain||!owner.destroySwapchain||!owner.endFrame)throw std::runtime_error("incomplete runtime dispatch");
        instances[*instance]=owner;
    }catch(...){
        PFN_xrVoidFunction raw=nullptr;if(nextInfo->nextGetInstanceProcAddr(*instance,"xrDestroyInstance",&raw)==XR_SUCCESS&&raw)reinterpret_cast<PFN_xrDestroyInstance>(raw)(*instance);
        *instance=XR_NULL_HANDLE;return XR_ERROR_INITIALIZATION_FAILED;
    }
    if(provide)report(Level::Important,"providing XR_FB_passthrough");
    return XR_SUCCESS;
}
} // namespace

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loader,const char* layerName,XrNegotiateApiLayerRequest* request){
    if(!loader||!request||!layerName||std::strcmp(layerName,LayerName)||loader->structType!=XR_LOADER_INTERFACE_STRUCT_LOADER_INFO||
       request->structType!=XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST||loader->minInterfaceVersion>XR_CURRENT_LOADER_API_LAYER_VERSION||
       loader->maxInterfaceVersion<XR_CURRENT_LOADER_API_LAYER_VERSION)return XR_ERROR_INITIALIZATION_FAILED;
    Level level{};if(stream_bridge::logging::parse(std::getenv("SFNP_LOG_LEVEL"),level))stream_bridge::logging::current.store(level,std::memory_order_relaxed);
    request->layerInterfaceVersion=XR_CURRENT_LOADER_API_LAYER_VERSION;request->layerApiVersion=loader->maxApiVersion<XR_CURRENT_API_VERSION?loader->maxApiVersion:XR_CURRENT_API_VERSION;
    request->getInstanceProcAddr=getInstanceProcAddr;request->createApiLayerInstance=createApiLayerInstance;
    return XR_SUCCESS;
}
