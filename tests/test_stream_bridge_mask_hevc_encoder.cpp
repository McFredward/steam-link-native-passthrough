#include "pc/stream-bridge-mask-hevc.h"
#include "pc/stream-bridge-mask-hevc-platform.h"
#include "pc/stream-bridge-mask-hevc-stage.h"
#include "pc/stream-bridge-sender-provider-coverage-stages.h"
#include "tests/stream_bridge_hevc_fixture.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include "pc/stream-bridge-mask-hevc-syntax-trace.h"
#include <nvEncodeAPI.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>

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
    bool wrongStamp=false,wrongType=false,wrongStruct=false,nullOutput=false,emptyOutput=false,hwFailure=false,reenter=false,eosFail=false,flushWaitFail=false;std::uint32_t outputSize=0;
    NVENCSTATUS mapStatus=NV_ENC_SUCCESS,encodeStatus=NV_ENC_SUCCESS,lockStatus=NV_ENC_SUCCESS;
    std::uint32_t hardwareStatus=2;
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
    check(h.repeatSPSPPS&&!h.outputAUD&&h.idrPeriod==1&&h.chromaFormatIDC==1&&!h.pixelBitDepthMinus8&&!h.enableAlphaLayerEncoding,"independent Main8 420 with repeated parameter sets and no optional AUD");
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
NVENCSTATUS NVENCAPI map(void* s,NV_ENC_MAP_INPUT_RESOURCE* p){auto& x=f(s);if(x.mapStatus!=NV_ENC_SUCCESS)return x.mapStatus;if(x.mapFail)return NV_ENC_ERR_MAP_FAILED;auto& r=*static_cast<Resource*>(p->registeredResource);check(r.registered&&!r.mapped,"map fixed free input only");r.mapped=true;p->mappedResource=&r;p->mappedBufferFmt=NV_ENC_BUFFER_FORMAT_NV12;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI unmap(void* s,NV_ENC_INPUT_PTR p){auto& x=f(s);auto& r=*static_cast<Resource*>(p);check(r.mapped,"unmap actual live input");r.mapped=false;++x.unmaps;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI encode(void* s,NV_ENC_PIC_PARAMS* p){
    auto& x=f(s);
    if(p->encodePicFlags&NV_ENC_PIC_FLAG_EOS){++x.eos;if(x.eosFail)return NV_ENC_ERR_GENERIC;for(auto& o:x.outputs)if(o.submitted){o.complete=true;o.event->signaled=true;}static_cast<Event*>(p->completionEvent)->signaled=true;return NV_ENC_SUCCESS;}
    if(x.encodeBusy)return NV_ENC_ERR_ENCODER_BUSY;
    if(x.encodeStatus!=NV_ENC_SUCCESS)return x.encodeStatus;
    ++x.privateEncodes;auto& o=*static_cast<Output*>(p->outputBitstream);
    check(p->encodePicFlags==(NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS)&&p->pictureStruct==NV_ENC_PIC_STRUCT_FRAME,"every submitted picture forces independent IDR and parameter sets");
    check(p->completionEvent&&p->inputBuffer&&p->bufferFmt==NV_ENC_BUFFER_FORMAT_NV12,"async GPU input, no upload/readback");
    check(p->inputTimeStamp&&p->inputTimeStamp==p->frameIdx,"private nonzero token, independent of native opaque timestamp");
    o.token=p->inputTimeStamp;o.event=static_cast<Event*>(p->completionEvent);o.submitted=true;o.complete=false;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI lock(void* s,NV_ENC_LOCK_BITSTREAM* p){
    auto& x=f(s);++x.locks;check(p->doNotWait==1,"lock must never wait");auto& o=*static_cast<Output*>(p->outputBitstream);
    check(o.complete,"lock only after signaled completion");if(x.lockBusy)return NV_ENC_ERR_LOCK_BUSY;
    if(x.lockStatus!=NV_ENC_SUCCESS)return x.lockStatus;
    if(x.reenter){std::vector<std::uint8_t> bytes{99};check(x.encoder.tryTake(x.key(1),bytes)==MaskHevcTake::Busy&&bytes.empty(),"hot reentry refuses without waiting or exposing stale data");}
    o.locked=true;p->outputTimeStamp=o.token+(x.wrongStamp?1:0);p->pictureType=x.wrongType?NV_ENC_PIC_TYPE_P:NV_ENC_PIC_TYPE_IDR;
    p->pictureStruct=x.wrongStruct?NV_ENC_PIC_STRUCT_FIELD_TOP_BOTTOM:NV_ENC_PIC_STRUCT_FRAME;
    p->hwEncodeStatus=x.hwFailure?1:x.hardwareStatus;p->bitstreamBufferPtr=x.nullOutput?nullptr:o.bytes.data();
    p->bitstreamSizeInBytes=x.emptyOutput?0:(x.outputSize?x.outputSize:std::uint32_t(o.bytes.size()));return NV_ENC_SUCCESS;
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
int main(int argc,char** argv){
    using namespace stream_bridge::sender;
    {
        MaskHevcSyntaxTraceSlot slot;MaskHevcSyntaxTrace trace{};
        check(!slot.take(trace),"cold diagnostic cannot read an unpublished header snapshot");
        check(!slot.captureOnce({},64,64),"empty diagnostic does not consume the single capture");
        check(slot.captureOnce({HevcMainIdrFixture.data(),HevcMainIdrFixture.size()},64,64),"capture first actual HEVC header prefixes");
        check(!slot.captureOnce({HevcMainIdrFixture.data(),HevcMainIdrFixture.size()},128,64),"later images cannot replace a pending diagnostic");
        check(slot.take(trace)&&trace.width==64&&trace.height==64&&trace.count==4&&!trace.more,"cold diagnostic acquires one coherent first snapshot");
        for(unsigned i=0;i<4;++i)check(trace.nals[i].type==(i==3?20u:32u+i),"parameter-set and IDR NAL ordering retained in diagnostic");
        check(trace.nals[0].prefix[0]==0x40&&trace.nals[3].copied<=32,"exact owned parameter bytes and bounded picture prefix retained");
        check(!slot.take(trace)&&!slot.captureOnce({HevcMainIdrFixture.data(),HevcMainIdrFixture.size()},64,64),"consumed one-shot cannot grow a log or capture queue");
        MaskHevcSyntaxTraceSlot hugeSlot;
        check(!hugeSlot.captureOnce({HevcMainIdrFixture.data(),stream_bridge::MaximumHevcCoverageBitstreamBytes+1},64,64),"oversized diagnostic is refused before reading the pointer");
        std::vector<std::uint8_t> many;
        for(unsigned i=0;i<12;++i)many.insert(many.end(),{0,0,1,0x46,1,0x10});
        MaskHevcSyntaxTraceSlot manySlot;check(manySlot.captureOnce({many.data(),many.size()},64,64)&&manySlot.take(trace),"bounded extra-NAL diagnostic captured");
        check(trace.count==8&&trace.more,"NAL count stays bounded on arbitrarily fragmented output");
        const std::vector<std::uint8_t> noPicture(4096,0x55);MaskHevcSyntaxTraceSlot plain;
        check(plain.captureOnce({noPicture.data(),noPicture.size()},64,64)&&plain.take(trace)&&trace.count==0&&trace.prefixBytes==32&&trace.prefix[0]==0x55,"diagnostic keeps unknown leading bytes without inventing framing");
        MaskHevcSyntaxTraceSlot concurrent;bool published=false,taken=false;
        std::thread producer([&]{published=concurrent.captureOnce({HevcMainIdrFixture.data(),HevcMainIdrFixture.size()},64,64);});
        for(unsigned attempt=0;attempt<1024&&!taken;++attempt){taken=concurrent.take(trace);if(!taken)std::this_thread::yield();}
        producer.join();if(!taken)taken=concurrent.take(trace);
        check(published&&taken&&trace.width==64&&trace.height==64&&trace.count==4,"cross-thread header publication is coherent and consumed once");
        check(!concurrent.take(trace),"cross-thread snapshot cannot be consumed twice");
    }
    for(const auto hardwareStatus:{2u,0u,1u,3u,8u,0xffffffffu}){
        Fixture x;x.config.width=x.config.height=64;x.hardwareStatus=hardwareStatus;
        x.outputs[0].bytes.assign(HevcMainIdrFixture.begin(),HevcMainIdrFixture.end());
        check(x.configure(),"configure actual completed-state output regression");
        const auto key=x.key(1);check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit exact input before completed-state output");
        std::vector<std::uint8_t> output{99};
        check(x.encoder.tryTake(key,output)==MaskHevcTake::Pending&&output.empty()&&x.locks==0,"status alone cannot bypass the actual completion event");
        x.complete(0);const auto result=x.encoder.tryTake(key,output);
        if(hardwareStatus==2||hardwareStatus==0){
            check(result==MaskHevcTake::Ready&&output==x.outputs[0].bytes,"completed HW state2 and prior zero state return their exact output");
            std::vector<std::uint8_t> envelope;
            check(stream_bridge::wrapHevcCoverage({output.data(),output.size()},64,64,envelope),"completed-state output still passes actual independent-IDR parameter-set validation");
            stream_bridge::HevcCoverageView decoded;
            check(stream_bridge::inspectHevcCoverage({envelope.data(),envelope.size()},64,64,decoded),"completed-state mask reaches the unchanged bounded HEVC wire parser");
            check(x.encoder.diagnostics().ready==1,"completion status does not fabricate a ready counter");
        }else{
            check(result==MaskHevcTake::Refused&&output.empty(),"partial and unknown hardware states remain refused");
            check(x.encoder.diagnostics().rejectedFields==8,"unsupported state retains its hardware-status diagnostic");
        }
        check(x.coldWaits==0,"completion-state handling never waits in a hot callback");resetAndCheck(x);
    }
    if(argc==2&&std::strcmp(argv[1],"--completion-regression")==0){
        std::printf("%u completed-HW-state regression checks passed; synthetic API, real owned HEVC fixture\n",checks);return 0;
    }
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
    for(unsigned control=0;control<7;++control){
        Fixture x;check(x.configure(),"configure malformed-output fixture");const auto key=x.key(1);
        check(x.encoder.beginStage(key,&x.textures[0])&&x.encoder.submit(key),"submit malformed-output control");x.complete(0);
        x.wrongStamp=control==0;x.wrongType=control==1;x.wrongStruct=control==2;x.hwFailure=control==3;
        x.nullOutput=control==4;x.emptyOutput=control==5;if(control==6)x.outputSize=x.config.maxAccessUnitBytes+1;
        std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(key,out)==MaskHevcTake::Refused&&out.empty(),"wrong timestamp/P-frame/hardware error/oversized AU never returns mask");
        const auto diagnostic=x.encoder.diagnostics();
        check(diagnostic.stage==MaskHevcFailure::OutputValidation&&diagnostic.nativeStatus==0&&x.encoder.lastNvencStatus()==0,
            "local output rejection is distinguishable from NVIDIA invalid-parameter status8");
        check(diagnostic.rejectedFields==(control<6?(1u<<control):32u),"every rejected output field has its own fixed bit");
        check(diagnostic.expectedTimestamp==x.outputs[0].token&&diagnostic.returnedTimestamp==x.outputs[0].token+(control==0?1:0),"diagnostics preserve exact expected and observed output timestamps");
        check(diagnostic.mapped==1&&diagnostic.submitted==1&&diagnostic.locked==1&&diagnostic.ready==0,"output rejection counters locate successful native operations");
        check(!x.resources[0].mapped&&!x.outputs[0].locked,"malformed output still releases completed resources");resetAndCheck(x);
    }
    for(unsigned stage=0;stage<3;++stage){
        Fixture x;check(x.configure(),"configure actual status8 origin fixture");
        if(stage==0)x.mapStatus=NV_ENC_ERR_INVALID_PARAM;
        if(stage==1)x.encodeStatus=NV_ENC_ERR_INVALID_PARAM;
        if(stage==2)x.lockStatus=NV_ENC_ERR_INVALID_PARAM;
        const auto key=x.key(1);check(x.encoder.beginStage(key,&x.textures[0]),"stage actual native failure fixture");
        const bool submitted=x.encoder.submit(key);check(submitted==(stage==2),"only lock failure follows an accepted submission");
        if(submitted){x.complete(0);std::vector<std::uint8_t> out{99};check(x.encoder.tryTake(key,out)==MaskHevcTake::Refused&&out.empty(),"native lock failure exposes no bytes");}
        const auto diagnostic=x.encoder.diagnostics();
        const MaskHevcFailure expected[]={MaskHevcFailure::MapInput,MaskHevcFailure::EncodePicture,MaskHevcFailure::LockBitstream};
        check(diagnostic.stage==expected[stage]&&diagnostic.nativeStatus==8&&diagnostic.rejectedFields==0,"actual status8 names its native call without inventing a metadata failure");
        check(diagnostic.mapped==(stage>0?1u:0u)&&diagnostic.submitted==(stage==2?1u:0u)&&diagnostic.locked==0,"failed native call never increments its success counter");
        resetAndCheck(x);
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
    {
        using Stages=CoverageStages<MaskHevcStageState>;
        Stages stages;auto captured=stages.capture(9);check(bool(captured),"source stage is captured before mask pool reconfiguration");
        captured->receipt.input=9;captured->receipt.contract=17;captured->receipt.present=1;captured->receipt.width=64;captured->receipt.height=64;
        captured->resources.hevcSurfaceReady=true;captured->resources.hevcSubmitted=true;captured->resources.hevcKey.submission=7;
        captured.publish(Stages::Phase::Captured);
        {Stages::PoolClaim maintenance;check(stages.maintenance(maintenance),"cold pool can retain an unbound captured A8 receipt");invalidateMaskHevcStages(maintenance);}
        check(stages.bind(9,64,64,7,reinterpret_cast<void*>(17),3,1),"retained original A8 receipt still binds to exact native input");
        auto bound=stages.read(7,reinterpret_cast<void*>(17),3);check(bool(bound),"exact retained native stage is readable");
        check(!bound->resources.hevcSurfaceReady&&!bound->resources.hevcSubmitted&&!bound->resources.hevcKey.submission,
            "captured NV12 write/key cannot survive cold pool replacement or failed reset");
        check(bound->receipt.contract==17&&bound->receipt.present==1,"NV12 invalidation preserves independent current A8 source proof");
    }
    {
        Fixture x;check(x.configure(),"configure independent mask warm-up pipeline");
        using Stages=CoverageStages<MaskHevcStageState>;Stages stages;
        for(unsigned i=0;i<4;++i){
            auto stage=stages.capture(9+i,i);check(stage&&stage.index()==i,"retired RGB stage rotates to another independent mask input");
            const auto key=x.key(i+1);check(x.encoder.canStage(&x.textures[i]),"warm-up uses a free NV12 surface instead of overwriting pending slot0");
            check(x.encoder.beginStage(key,&x.textures[i])&&x.encoder.submit(key),"four distinct exact inputs can enter asynchronous warm-up");
            std::vector<std::uint8_t> output;
            check(x.encoder.tryTake(key,output)==MaskHevcTake::Pending,"delayed mask output keeps per-frame lossless fallback possible");
            x.encoder.cancel(key);stage.publish(Stages::Phase::Retired);
        }
        check(x.privateEncodes==4,"prompt RGB retirement cannot starve independent mask encoder of subsequent inputs");
        for(unsigned i=0;i<4;++i)x.complete(i);
        check(x.encoder.canStage(&x.textures[0]),"completed canceled warm-up input safely becomes reusable");resetAndCheck(x);
    }
    std::printf("mask HEVC private-session lifetime/association tests: %u checks passed; no hardware codec claim\n",checks);
}
