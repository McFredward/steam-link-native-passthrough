#include "openxr/openxr_frame_policy.h"
#include "openxr/openxr_source_record.h"
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
using namespace stream_bridge::openxr;
namespace {
unsigned checks=0;void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
SourceMessage reply(std::uint32_t pid,std::uint32_t server,const SourceNonce& nonce,bool budget,bool granted){
    SourceMessage out{};std::memcpy(out.data(),"SBN1",4);out[4]=2;out[6]=budget?2:1;out[8]=128;out[12]=1;
    for(unsigned i=0;i<4;++i){out[16+i]=std::uint8_t(pid>>(8*i));out[20+i]=std::uint8_t(server>>(8*i));}
    std::memcpy(out.data()+24,nonce.data(),16);
    if(granted){out[10]=budget?5:1;out[40]=9;out[56]=17;}
    return out;
}
void records(){
    SourceNonce nonce{};nonce[3]=7;
    const auto v1=sourceRequest(4242,nonce,false),v2=sourceRequest(4242,nonce,true);
    check(!std::memcmp(v1.data(),"SBN1",4)&&v1[4]==1&&v1[6]==1&&v1[8]==128&&v1[10]==0&&v1[12]==1&&v1[16]==0x92&&v1[17]==0x10&&v1[20]==0&&v1[27]==7,"v1 request layout");
    check(v2[6]==2,"v2 request asks for the metadata budget");
    for(unsigned i=40;i<128;++i)check(!v1[i],"request reserved bytes stay zero");
    auto pending=readSourceReply(reply(4242,77,nonce,false,false),4242,77,nonce,false);
    check(pending.valid&&!pending.granted,"a present sender that has not admitted the process");
    auto granted=readSourceReply(reply(4242,77,nonce,true,true),4242,77,nonce,true);
    check(granted.valid&&granted.granted&&granted.budget&&granted.generation==17&&granted.binding[0]==9,"v2 grant with binding and generation");
    check(readSourceReply(reply(4242,77,nonce,false,true),4242,77,nonce,false).granted,"v1 grant");
    auto bad=reply(4242,77,nonce,true,true);
    check(!readSourceReply(bad,4242,77,nonce,false).valid,"a v2 reply cannot answer a v1 request");
    check(!readSourceReply(reply(4243,77,nonce,false,true),4242,77,nonce,false).valid,"another process's reply");
    check(!readSourceReply(reply(4242,78,nonce,false,true),4242,77,nonce,false).valid,"another server's reply");
    SourceNonce other=nonce;other[0]=1;
    check(!readSourceReply(reply(4242,77,other,false,true),4242,77,nonce,false).valid,"another conversation's nonce");
    bad=reply(4242,77,nonce,false,true);bad[100]=1;check(!readSourceReply(bad,4242,77,nonce,false).valid,"nonzero reserved bytes");
    bad=reply(4242,77,nonce,false,true);bad[40]=0;check(!readSourceReply(bad,4242,77,nonce,false).valid,"a grant without binding");
    bad=reply(4242,77,nonce,false,false);bad[56]=3;check(!readSourceReply(bad,4242,77,nonce,false).valid,"a refusal with a generation");
    bad=reply(4242,77,nonce,true,true);bad[10]=1;check(!readSourceReply(bad,4242,77,nonce,true).valid,"a v2 grant must carry the budget flag");
    bad=reply(4242,77,nonce,false,false);bad[10]=2;check(readSourceReply(bad,4242,77,nonce,false).valid,"reserved telemetry flag2 is tolerated");
    check(!readSourceReply(reply(4242,77,SourceNonce{},false,true),4242,77,SourceNonce{},false).valid,"a zero nonce never validates");
}
void blendModes(){
    std::vector<XrEnvironmentBlendMode> runtime{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};
    std::uint32_t count=0;check(withAlphaBlend(runtime,0,&count,nullptr)==XR_SUCCESS&&count==2,"count includes ALPHA_BLEND");
    XrEnvironmentBlendMode modes[2]{};
    check(withAlphaBlend(runtime,1,&count,modes)==XR_ERROR_SIZE_INSUFFICIENT,"two-call capacity");
    check(withAlphaBlend(runtime,2,&count,modes)==XR_SUCCESS&&modes[0]==XR_ENVIRONMENT_BLEND_MODE_OPAQUE&&modes[1]==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,"runtime preference first");
    runtime.push_back(XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
    check(withAlphaBlend(runtime,0,&count,nullptr)==XR_SUCCESS&&count==2,"never duplicated");
    check(withAlphaBlend(runtime,0,nullptr,nullptr)==XR_ERROR_VALIDATION_FAILURE,"count output is required");
}
struct Scene {
    std::map<XrSwapchain,SwapchainShape> swapchains;std::map<XrPassthroughLayerFB,PassthroughLayerState> layers;
    XrCompositionLayerProjectionView views[2]{};XrCompositionLayerProjection projection{};XrCompositionLayerPassthroughFB passthrough{};XrCompositionLayerQuad quad{};
    Scene(){
        for(unsigned i=0;i<2;++i){views[i].type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;views[i].subImage.swapchain=reinterpret_cast<XrSwapchain>(std::uintptr_t(10+i));
            swapchains[views[i].subImage.swapchain]={29,1,1,1};}
        projection.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;projection.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;projection.viewCount=2;projection.views=views;
        passthrough.type=XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB;passthrough.layerHandle=reinterpret_cast<XrPassthroughLayerFB>(std::uintptr_t(5));
        layers[passthrough.layerHandle]={true,true,1};quad.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    FramePlan plan(std::vector<const XrCompositionLayerBaseHeader*> list,XrEnvironmentBlendMode mode,bool blend=true,bool fb=true,Graphics api=Graphics::D3D11){
        XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};info.environmentBlendMode=mode;info.layerCount=std::uint32_t(list.size());info.layers=list.data();
        return planFrame(info,blend,fb,api,[&](XrPassthroughLayerFB h){auto it=layers.find(h);return it==layers.end()?PassthroughLayerState{}:it->second;},
            [&](XrSwapchain h,SwapchainShape& s){auto it=swapchains.find(h);if(it==swapchains.end())return false;s=it->second;return true;});
    }
    const XrCompositionLayerBaseHeader* base(const void* layer){return static_cast<const XrCompositionLayerBaseHeader*>(layer);}
};
void frames(){
    {
        Scene s;auto p=s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
        check(p.mode==XR_ENVIRONMENT_BLEND_MODE_OPAQUE&&p.requested&&p.coverage&&p.layers.size()==1,"ALPHA_BLEND becomes OPAQUE for SteamVR with coverage declared");
        p=s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND,false);
        check(p.mode==XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND&&!p.requested,"a runtime's own ALPHA_BLEND passes through untouched");
        p=s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.requested&&!p.coverage&&p.refusal==FrameRefusal::NotRequested,"ordinary VR declares nothing");
    }
    {
        Scene s;auto p=s.plan({s.base(&s.passthrough),s.base(&s.projection),s.base(&s.quad)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(p.coverage&&p.layers.size()==2&&p.layers[0]==s.base(&s.projection)&&p.layers[1]==s.base(&s.quad),"FB layer under content is removed and declared");
        s.layers[s.passthrough.layerHandle].running=false;p=s.plan({s.base(&s.passthrough),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.requested&&p.layers.size()==1,"a paused passthrough layer is removed without a declaration");
        s.layers[s.passthrough.layerHandle]={true,true,0};p=s.plan({s.base(&s.passthrough),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.requested,"opacity zero hides the camera");
        s.layers.clear();p=s.plan({s.base(&s.passthrough),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.requested&&p.layers.size()==1,"unknown passthrough handles never request the camera");
        p=s.plan({s.base(&s.passthrough),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE,true,false);
        check(!p.requested&&p.layers.size()==2,"a runtime's own XR_FB_passthrough layers pass through");
    }
    {
        Scene s;auto p=s.plan({s.base(&s.projection),s.base(&s.passthrough)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(p.requested&&!p.coverage&&p.refusal==FrameRefusal::LayerAboveContent&&p.layers.size()==1,"camera above content cannot be represented");
        p=s.plan({s.base(&s.passthrough),s.base(&s.quad),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.coverage&&p.refusal==FrameRefusal::NoProjection,"bottom content must be the projection");
        s.projection.layerFlags=0;p=s.plan({s.base(&s.passthrough),s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
        check(!p.coverage&&p.refusal==FrameRefusal::AlphaIgnored,"alpha the application marked as ignored is never coverage");
        s.projection.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
        p=s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
        check(!p.coverage&&p.refusal==FrameRefusal::Unpremultiplied&&p.mode==XR_ENVIRONMENT_BLEND_MODE_OPAQUE,"straight alpha stays opaque");
    }
    {
        Scene s;s.swapchains[s.views[1].subImage.swapchain].arraySize=2;
        check(s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND).refusal==FrameRefusal::SwapchainShape,"texture arrays are not admitted by the sender");
        s.swapchains[s.views[1].subImage.swapchain]={29,1,1,4};
        check(s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND).refusal==FrameRefusal::SwapchainShape,"multisampled swapchains");
        s.swapchains[s.views[1].subImage.swapchain]={10,1,1,1};
        check(s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND).refusal==FrameRefusal::SwapchainFormat,"half-float swapchains");
        s.swapchains.erase(s.views[1].subImage.swapchain);
        check(s.plan({s.base(&s.projection)},XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND).refusal==FrameRefusal::SwapchainShape,"unknown swapchains");
    }
    check(ordinaryRgba(Graphics::D3D11,28)&&ordinaryRgba(Graphics::D3D12,91)&&ordinaryRgba(Graphics::Vulkan,43)&&ordinaryRgba(Graphics::Vulkan,44)&&ordinaryRgba(Graphics::OpenGL,0x8C43),"8-bit RGBA/BGRA per API");
    check(!ordinaryRgba(Graphics::Vulkan,28)&&!ordinaryRgba(Graphics::Unknown,28)&&!ordinaryRgba(Graphics::D3D11,24),"other formats or unknown APIs");
    check(std::string(refusalName(FrameRefusal::Unpremultiplied))=="unpremultiplied alpha"&&std::string(refusalName(FrameRefusal::Count))=="unknown","refusal names");
}
}
int main(){try{records();blendModes();frames();std::printf("OpenXR layer policy: %u checks PASS\n",checks);return 0;}catch(const std::exception&e){std::fprintf(stderr,"OpenXR layer policy failed after %u: %s\n",checks,e.what());return 1;}}
