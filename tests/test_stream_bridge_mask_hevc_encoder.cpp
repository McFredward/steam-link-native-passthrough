#include "pc/stream-bridge-mask-hevc.h"
#include "pc/stream-bridge-mask-hevc-platform.h"
#include <nvEncodeAPI.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// Same encoder implementation, original API table and event state transitions
// as Windows, with no real GPU/OS handles. These are contract/lifetime tests,
// not a claim that hardware produces a conforming HEVC Main bitstream.
struct ID3D11Device {unsigned refs=0;};
struct ID3D11Texture2D {ID3D11Device* device=nullptr;unsigned refs=0;bool valid=true;};
namespace {
using namespace stream_bridge::sender;
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
struct Event {bool signaled=false;};
struct Resource {ID3D11Texture2D* texture=nullptr;bool registered=false,mapped=false;};
struct Output {
    std::vector<std::uint8_t> bytes{0,0,0,1,0x26,1,0x80};
    Event* event=nullptr;std::uint64_t token=0;bool allocated=false,submitted=false,complete=false,locked=false;
};
struct Fixture;
Fixture* active=nullptr;
struct Fixture {
    ID3D11Device device{};std::array<ID3D11Texture2D,4> textures{};
    std::array<Resource,4> resources{};std::array<Output,4> outputs{};std::vector<Event*> events;
    NV_ENCODE_API_FUNCTION_LIST api{};MaskHevcConfig config{1152,4608,90,1,20000000,2u*1024u*1024u};
    unsigned opened=0,destroyed=0,created=0,registered=0,locks=0,unmaps=0,coldWaits=0,eos=0,privateEncodes=0,polled=0;
    bool async=true,nv12=true,failOpen=false,failInit=false,failPreset=false,mapFail=false,encodeBusy=false,lockBusy=false;
    bool wrongStamp=false,wrongType=false,hwFailure=false,reenter=false,eosFail=false,flushWaitFail=false;std::uint32_t outputSize=0;
    int failResource=-1,failBuffer=-1,failEvent=-1;
    MaskHevcEncoder encoder;
    Fixture();
    std::array<ID3D11Texture2D*,4> pointers(){return {&textures[0],&textures[1],&textures[2],&textures[3]};}
    bool configure(){return encoder.configure(&device,config,pointers(),api);}
    MaskHevcKey key(std::uint64_t n)const{return {17,23,n,31,37,0,43,config.width,config.height};}
    void complete(unsigned i){auto& o=outputs[i];check(o.submitted,"complete submitted output only");o.complete=true;o.event->signaled=true;}
};
Fixture& f(void* encoder){check(active&&encoder==active,"private original NVENC session only");return *active;}
NVENCSTATUS NVENCAPI open(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS* p,void** s){
    auto& x=*active;check(p->device==&x.device&&p->deviceType==NV_ENC_DEVICE_TYPE_DIRECTX,"correct native D3D11 device");
    check(p->version==NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER&&p->apiVersion==NVENCAPI_VERSION,"pinned NVENC API version");
    if(x.failOpen)return NV_ENC_ERR_NO_ENCODE_DEVICE;
    ++x.opened;*s=&x;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI destroy(void* s){auto& x=f(s);++x.destroyed;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI caps(void* s,GUID,NV_ENC_CAPS_PARAM* p,int* out){auto& x=f(s);*out=p->capsToQuery==NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT?int(x.async):8192;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI formatCount(void* s,GUID,std::uint32_t* n){f(s);*n=1;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI formats(void* s,GUID,NV_ENC_BUFFER_FORMAT* out,std::uint32_t n,std::uint32_t* actual){
    auto& x=f(s);check(n==1,"bounded input format query");*out=x.nv12?NV_ENC_BUFFER_FORMAT_NV12:NV_ENC_BUFFER_FORMAT_ARGB;*actual=1;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI preset(void* s,GUID,GUID,NV_ENC_TUNING_INFO tune,NV_ENC_PRESET_CONFIG* p){
    auto& x=f(s);check(tune==NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,"ultra-low-latency preset request");
    if(x.failPreset)return NV_ENC_ERR_UNSUPPORTED_PARAM;
    // Host deliberately returns hostile optional preset features. The module
    // must explicitly clear AQ/lookahead/alpha/multislice inherited defaults.
    p->presetCfg.rcParams.enableAQ=1;p->presetCfg.rcParams.enableTemporalAQ=1;p->presetCfg.rcParams.enableLookahead=1;
    p->presetCfg.encodeCodecConfig.hevcConfig.enableAlphaLayerEncoding=1;p->presetCfg.encodeCodecConfig.hevcConfig.sliceMode=3;
    return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI init(void* s,NV_ENC_INITIALIZE_PARAMS* p){
    auto& x=f(s);check(p->enableEncodeAsync==1&&p->enablePTD==1&&!p->enableOutputInVidmem,"async CPU-readable output only");
    check(p->encodeWidth==x.config.width&&p->encodeHeight==x.config.height&&p->frameRateNum==x.config.frameRateNum&&p->frameRateDen==x.config.frameRateDen,"unmodified configured extent/FPS");
    check(p->encodeConfig&&p->encodeConfig->frameIntervalP==1&&p->encodeConfig->gopLength==1,"independent-picture GOP configuration");
    const auto& c=*p->encodeConfig;const auto& h=c.encodeCodecConfig.hevcConfig;const auto& r=c.rcParams;
    check(std::memcmp(&c.profileGUID,&NV_ENC_HEVC_PROFILE_MAIN_GUID,sizeof(GUID))==0,"Main profile is explicit");
    check(h.repeatSPSPPS&&h.idrPeriod==1&&h.chromaFormatIDC==1&&!h.pixelBitDepthMinus8&&!h.enableAlphaLayerEncoding,"independent Main8 420 with repeated parameter sets");
    check(!h.sliceMode&&!h.sliceModeData&&!h.enableIntraRefresh&&!h.enableLTR,"one slice, no refresh/reference features");
    check(h.hevcVUIParameters.videoSignalTypePresentFlag&&h.hevcVUIParameters.videoFullRangeFlag&&!h.hevcVUIParameters.colourDescriptionPresentFlag,"raw full-range Y without color transform signaling");
    check(r.rateControlMode==NV_ENC_PARAMS_RC_CBR&&r.averageBitRate==x.config.bitRate&&r.maxBitRate==x.config.bitRate,"configured mask CBR target");
    check(r.vbvBufferSize==(std::uint64_t(x.config.bitRate)*x.config.frameRateDen+x.config.frameRateNum-1)/x.config.frameRateNum&&r.vbvInitialDelay==r.vbvBufferSize,"one-frame VBV in bits with safe rational rounding");
    check(!r.enableAQ&&!r.enableTemporalAQ&&!r.enableLookahead&&!r.lookaheadDepth&&r.zeroReorderDelay&&r.multiPass==NV_ENC_MULTI_PASS_DISABLED,"no hidden lookahead, AQ, multipass or reordering");
    return x.failInit?NV_ENC_ERR_INVALID_PARAM:NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI reg(void* s,NV_ENC_REGISTER_RESOURCE* p){
    auto& x=f(s);const auto index=x.registered++;if(int(index)==x.failResource)return NV_ENC_ERR_RESOURCE_REGISTER_FAILED;
    check(index<4&&p->resourceType==NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX&&p->bufferFormat==NV_ENC_BUFFER_FORMAT_NV12&&p->bufferUsage==NV_ENC_INPUT_IMAGE&&!p->pitch,"fixed original GPU NV12 resource only");
    auto& r=x.resources[index];r={static_cast<ID3D11Texture2D*>(p->resourceToRegister),true,false};p->registeredResource=&r;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI unreg(void* s,NV_ENC_REGISTERED_PTR p){f(s);auto& r=*static_cast<Resource*>(p);check(!r.mapped,"unregister only unmapped input");r.registered=false;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI createBuffer(void* s,NV_ENC_CREATE_BITSTREAM_BUFFER* p){auto& x=f(s);const auto i=x.created++;if(int(i)==x.failBuffer)return NV_ENC_ERR_OUT_OF_MEMORY;check(i<4,"four bounded bitstream buffers");auto& o=x.outputs[i];o.allocated=true;p->bitstreamBuffer=&o;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI destroyBuffer(void* s,NV_ENC_OUTPUT_PTR p){f(s);auto& o=*static_cast<Output*>(p);check(!o.locked&&(!o.submitted||o.complete),"destroy after GPU completion/unlock");o.allocated=false;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI regEvent(void* s,NV_ENC_EVENT_PARAMS*){auto& x=f(s);return int(x.events.size())==x.failEvent?NV_ENC_ERR_EVENT_NOT_REGISTERD:NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI unregEvent(void* s,NV_ENC_EVENT_PARAMS*){f(s);return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI map(void* s,NV_ENC_MAP_INPUT_RESOURCE* p){auto& x=f(s);if(x.mapFail)return NV_ENC_ERR_MAP_FAILED;auto& r=*static_cast<Resource*>(p->registeredResource);check(r.registered&&!r.mapped,"map fixed free input only");r.mapped=true;p->mappedResource=&r;p->mappedBufferFmt=NV_ENC_BUFFER_FORMAT_NV12;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI unmap(void* s,NV_ENC_INPUT_PTR p){auto& x=f(s);auto& r=*static_cast<Resource*>(p);check(r.mapped,"unmap actual live input");r.mapped=false;++x.unmaps;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI encode(void* s,NV_ENC_PIC_PARAMS* p){
    auto& x=f(s);
    if(p->encodePicFlags&NV_ENC_PIC_FLAG_EOS){++x.eos;if(x.eosFail)return NV_ENC_ERR_GENERIC;for(auto& o:x.outputs)if(o.submitted){o.complete=true;o.event->signaled=true;}static_cast<Event*>(p->completionEvent)->signaled=true;return NV_ENC_SUCCESS;}
    if(x.encodeBusy)return NV_ENC_ERR_ENCODER_BUSY;
    ++x.privateEncodes;auto& o=*static_cast<Output*>(p->outputBitstream);
    check(p->encodePicFlags==(NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS)&&p->pictureStruct==NV_ENC_PIC_STRUCT_FRAME,"every submitted picture forces independent IDR and parameter sets");
    check(p->completionEvent&&p->inputBuffer&&p->bufferFmt==NV_ENC_BUFFER_FORMAT_NV12,"async GPU input, no upload/readback");
    check(p->inputTimeStamp&&p->inputTimeStamp==p->frameIdx,"private nonzero token, independent of native opaque timestamp");
    o.token=p->inputTimeStamp;o.event=static_cast<Event*>(p->completionEvent);o.submitted=true;o.complete=false;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI lock(void* s,NV_ENC_LOCK_BITSTREAM* p){
    auto& x=f(s);++x.locks;check(p->doNotWait==1,"lock must never wait");auto& o=*static_cast<Output*>(p->outputBitstream);
    check(o.complete,"lock only after signaled completion");if(x.lockBusy)return NV_ENC_ERR_LOCK_BUSY;
    if(x.reenter){std::vector<std::uint8_t> bytes{99};check(x.encoder.tryTake(x.key(1),bytes)==MaskHevcTake::Busy&&bytes.empty(),"hot reentry refuses without waiting or exposing stale data");}
    o.locked=true;p->outputTimeStamp=o.token+(x.wrongStamp?1:0);p->pictureType=x.wrongType?NV_ENC_PIC_TYPE_P:NV_ENC_PIC_TYPE_IDR;
    p->pictureStruct=NV_ENC_PIC_STRUCT_FRAME;p->hwEncodeStatus=x.hwFailure?1:0;p->bitstreamBufferPtr=o.bytes.data();
    p->bitstreamSizeInBytes=x.outputSize?x.outputSize:std::uint32_t(o.bytes.size());return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI unlock(void* s,NV_ENC_OUTPUT_PTR p){f(s);auto& o=*static_cast<Output*>(p);check(o.locked,"unlock actual locked buffer");o.locked=false;o.submitted=false;return NV_ENC_SUCCESS;}
Fixture::Fixture(){
    active=this;for(auto& t:textures)t.device=&device;
    api.version=NV_ENCODE_API_FUNCTION_LIST_VER;api.nvEncOpenEncodeSessionEx=open;api.nvEncDestroyEncoder=destroy;
    api.nvEncGetEncodeCaps=caps;api.nvEncGetInputFormatCount=formatCount;api.nvEncGetInputFormats=formats;
    api.nvEncGetEncodePresetConfigEx=preset;api.nvEncInitializeEncoder=init;api.nvEncRegisterResource=reg;api.nvEncUnregisterResource=unreg;
    api.nvEncCreateBitstreamBuffer=createBuffer;api.nvEncDestroyBitstreamBuffer=destroyBuffer;api.nvEncRegisterAsyncEvent=regEvent;
    api.nvEncUnregisterAsyncEvent=unregEvent;api.nvEncMapInputResource=map;api.nvEncUnmapInputResource=unmap;
    api.nvEncEncodePicture=encode;api.nvEncLockBitstream=lock;api.nvEncUnlockBitstream=unlock;
}
void resetAndCheck(Fixture& x){
    check(x.encoder.reset(),"cold reset completes safely");check(x.opened==x.destroyed&&x.events.empty()&&x.device.refs==0,"all private session/event/device references released");
    for(auto& t:x.textures)check(t.refs==0,"all GPU texture references released");
    for(const auto& r:x.resources)check(!r.registered&&!r.mapped,"no registered or mapped input survives reset");
    for(const auto& o:x.outputs)check(!o.allocated&&!o.locked,"no output allocation/lock survives reset");
}
}
namespace stream_bridge::sender::detail {
MaskHevcPlatform maskHevcTestPlatform()noexcept{
    return {
        [](ID3D11Device* d,ID3D11Texture2D* t,std::uint32_t,std::uint32_t)noexcept{return t&&t->device==d&&t->valid;},
        [](ID3D11Device* p)noexcept{++p->refs;},[](ID3D11Device* p)noexcept{--p->refs;},
        [](ID3D11Texture2D* p)noexcept{++p->refs;},[](ID3D11Texture2D* p)noexcept{--p->refs;},
        []()noexcept->void*{auto* p=new(std::nothrow) Event;if(p)active->events.push_back(p);return p;},
        [](void* p)noexcept{auto& e=active->events;e.erase(std::find(e.begin(),e.end(),p));delete static_cast<Event*>(p);},
        [](void* p)noexcept{static_cast<Event*>(p)->signaled=false;return true;},
        [](void* p)noexcept{++active->polled;auto& e=*static_cast<Event*>(p);const bool ready=e.signaled;e.signaled=false;return ready?1:0;},
        [](void* p)noexcept{++active->coldWaits;return !active->flushWaitFail&&static_cast<Event*>(p)->signaled;}
    };
}
}
int main(){
    using namespace stream_bridge::sender;
    {
        Fixture x;check(x.configure()&&x.encoder.configured(),"configure separate async encoder");
        check(x.created==4&&x.events.size()==5,"four bounded output events plus one cold flush event");
        auto a=x.key(1),b=x.key(2);b.inputTimestamp=a.inputTimestamp=~std::uint64_t(0);
        check(x.encoder.canStage(&x.textures[0])&&x.encoder.beginStage(a,&x.textures[0])&&x.encoder.submit(a),"first exact native frame submitted");
        check(x.encoder.beginStage(b,&x.textures[1])&&x.encoder.submit(b),"second exact frame, same opaque timestamp");
        check(x.outputs[0].token!=x.outputs[1].token,"private timestamp disambiguates opaque native timestamp reuse");
        std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(a,out)==MaskHevcTake::Pending&&out.empty()&&x.locks==0,"pending never locks or exposes old bytes");
        for(unsigned field=0;field<9;++field){auto wrong=a;switch(field){case 0:++wrong.sourceContract;break;case 1:++wrong.encoderGeneration;break;case 2:wrong.submission+=20;break;case 3:++wrong.resourceGeneration;break;case 4:++wrong.mapGeneration;break;case 5:++wrong.inputTimestamp;break;case 6:++wrong.processId;break;case 7:++wrong.width;break;default:++wrong.height;break;}check(x.encoder.tryTake(wrong,out)==MaskHevcTake::Missing&&out.empty(),"every key component is exact");}
        x.complete(1);check(x.encoder.tryTake(a,out)==MaskHevcTake::Pending,"out-of-order completion never substitutes newer frame");
        check(x.encoder.tryTake(b,out)==MaskHevcTake::Ready&&out==x.outputs[1].bytes,"only exact completed second frame returned");
        check(x.encoder.canStage(&x.textures[1]),"completed second texture can be reused");
        x.complete(0);x.reenter=true;check(x.encoder.tryTake(a,out)==MaskHevcTake::Ready,"exact first output with bounded reentry");
        check(!x.encoder.beginStage(a,&x.textures[0]),"completed native submission cannot be replayed");
        check(x.coldWaits==0,"no hot method waited on events");resetAndCheck(x);check(x.coldWaits==1,"only cold reset flush waits");
    }
    {
        Fixture x;check(x.configure(),"configure canceled-input fixture");
        for(unsigned i=0;i<4;++i){const auto key=x.key(i+1);check(x.encoder.beginStage(key,&x.textures[i])&&x.encoder.submit(key),"fill bounded four-slot pipeline");x.encoder.cancel(key);check(!x.encoder.canStage(&x.textures[i]),"cancel does not recycle pending GPU input");}
        x.complete(2);check(x.encoder.canStage(&x.textures[2]),"canceled texture reusable after completion/unlock/unmap");
        check(!x.encoder.canStage(&x.textures[0])&&x.coldWaits==0,"poll canceled slot does not wait for other slots");
        resetAndCheck(x);check(x.coldWaits==1,"cold reset flushes pending canceled inputs");
    }
    {
        Fixture x;check(x.configure(),"configure signaled-then-busy fixture");const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit before simulated lock busy");x.complete(0);
        x.lockBusy=true;std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(key,out)==MaskHevcTake::Pending&&out.empty(),"driver lock busy is a nonblocking pending result");
        x.lockBusy=false;check(x.encoder.tryTake(key,out)==MaskHevcTake::Ready,"consumed auto-reset event remains recorded for retry");resetAndCheck(x);
    }
    for(unsigned control=0;control<4;++control){
        Fixture x;check(x.configure(),"configure malformed-output fixture");const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit malformed-output control");x.complete(0);
        x.wrongStamp=control==0;x.wrongType=control==1;x.hwFailure=control==2;if(control==3)x.outputSize=x.config.maxAccessUnitBytes+1;
        std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(key,out)==MaskHevcTake::Refused&&out.empty(),"wrong timestamp/P-frame/hardware error/oversized AU never returns mask");
        check(!x.resources[0].mapped&&!x.outputs[0].locked,"malformed output still releases completed resources");resetAndCheck(x);
    }
    for(unsigned control=0;control<2;++control){
        Fixture x;check(x.configure(),"configure submission-error fixture");x.mapFail=control==0;x.encodeBusy=control==1;const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&!x.encoder.submit(key),"map/encode error refuses this frame");
        check(x.encoder.canStage(&x.textures[0])&&!x.resources[0].mapped,"rejected submission does not retain input");
        check(x.encoder.lastNvencStatus()!=int(NV_ENC_SUCCESS),"error survives successful cleanup for diagnostics");resetAndCheck(x);
    }
    for(unsigned control=0;control<2;++control){
        Fixture x;check(x.configure(),"configure cold-flush failure fixture");const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit live GPU input before flush failure");
        x.eosFail=control==0;x.flushWaitFail=control==1;
        check(!x.encoder.reset()&&!x.encoder.configured(),"failed flush deactivates session without freeing live resources");
        check(x.resources[0].mapped&&x.outputs[0].allocated&&x.events.size()==5&&x.device.refs==1,"all GPU-live handles retained after failed flush");
        check(!x.encoder.canStage(&x.textures[0]),"failed-flush texture cannot be overwritten");
        const unsigned oldOpen=x.opened;
        check(!x.configure()&&x.opened==oldOpen,"failed flush cannot allocate a replacement session");
        x.eosFail=x.flushWaitFail=false;const auto oldEos=x.eos;resetAndCheck(x);
        check(control==0||x.eos==oldEos,"pending flush retry waits on original event, no duplicate EOS");
    }
    {
        Fixture x;check(x.configure(),"configure missed-cancel reuse fixture");const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit old native frame before external retirement");
        check(!x.encoder.canStage(&x.textures[0]),"external retired-slot claim cannot overwrite pending input");
        x.complete(0);check(x.encoder.canStage(&x.textures[0]),"reuse claim retires completed old output without a prior cancel");
        std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(key,out)==MaskHevcTake::Missing&&out.empty(),"abandoned output cannot later return as an exact image");resetAndCheck(x);
    }
    {
        Fixture x;x.config.frameRateNum=90000;x.config.frameRateDen=1001;check(x.configure(),"rational nominal FPS uses correct one-frame VBV");
        const auto key=x.key(1);x.outputs[0].bytes.assign(x.config.maxAccessUnitBytes,127);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit exact maximum-AU boundary");x.complete(0);
        std::vector<std::uint8_t> out;check(x.encoder.tryTake(key,out)==MaskHevcTake::Ready&&out==x.outputs[0].bytes,"exact configured AU boundary accepted without truncation");resetAndCheck(x);
    }
    for(unsigned control=0;control<3;++control){
        Fixture x;auto textures=x.pointers();
        if(control==0)textures[2]=textures[1];
        if(control==1)x.textures[2].valid=false;
        if(control==2)x.textures[2].device=nullptr;
        check(!x.encoder.configure(&x.device,x.config,textures,x.api)&&!x.opened,"duplicate/wrong-shape/wrong-device texture refused before opening session");resetAndCheck(x);
    }
    {
        Fixture x;check(x.configure(),"configure epoch fixture");auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0]),"bind initial source epoch");x.encoder.cancel(key);
        for(unsigned control=0;control<3;++control){key=x.key(control+2);if(control==0)++key.sourceContract;if(control==1)++key.processId;if(control==2)++key.encoderGeneration;check(!x.encoder.beginStage(key,&x.textures[0]),"source/PID/encoder change requires cold reset");}
        resetAndCheck(x);
    }
    for(unsigned control=0;control<12;++control){
        Fixture x;
        if(control==0)x.async=false;
        if(control==1)x.nv12=false;
        if(control==2)x.failOpen=true;
        if(control==3)x.failPreset=true;
        if(control==4)x.failInit=true;
        if(control==5)x.failResource=2;
        if(control==6)x.failBuffer=2;
        if(control==7)x.failEvent=3;
        if(control==8)x.config.width|=1;
        if(control==9)x.config.bitRate=0;
        if(control==10)x.api.version=0;
        if(control==11)x.api.nvEncLockBitstream=nullptr;
        check(!x.configure()&&!x.encoder.configured(),"unsupported capability/configuration or partial allocation fails closed");resetAndCheck(x);
    }
    std::printf("mask HEVC private-session lifetime/association tests: %u checks passed; no hardware codec claim\n",checks);
}
