#include "stream-bridge-mask-hevc.h"
#include "stream-bridge-mask-hevc-platform.h"
#include "stream-bridge-mask-hevc-rate.h"
#include <nvEncodeAPI.h> // Same explicitly supplied, hash-pinned SDK11.1 as provider.
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#include <d3d11.h>
#endif

namespace stream_bridge::sender {
static_assert(sizeof(void*)==8&&sizeof(NV_ENCODE_API_FUNCTION_LIST)==0x9f8,"reviewed SDK11.1 64-bit ABI");
static_assert(NV_ENCODE_API_FUNCTION_LIST_VER==0x7102000b,"reviewed SDK11.1 function table version");
bool operator==(const MaskHevcKey& a,const MaskHevcKey& b)noexcept{
    return a.sourceContract==b.sourceContract&&a.encoderGeneration==b.encoderGeneration&&a.submission==b.submission
        &&a.resourceGeneration==b.resourceGeneration&&a.mapGeneration==b.mapGeneration&&a.inputTimestamp==b.inputTimestamp
        &&a.processId==b.processId&&a.width==b.width&&a.height==b.height;
}
namespace {
constexpr std::uint32_t MaxPixels=8u*1024u*1024u,MaxAccessUnit=2u*1024u*1024u;
bool validConfig(const MaskHevcConfig& c)noexcept{
    return c.width&&c.height&&!(c.width&1)&&!(c.height&1)&&c.width<=8192&&c.height<=8192
        &&std::uint64_t(c.width)*c.height<=MaxPixels&&c.frameRateNum&&c.frameRateDen
        &&std::uint64_t(c.frameRateNum)<=std::uint64_t(c.frameRateDen)*240
        &&std::uint64_t(c.frameRateNum)>=c.frameRateDen&&c.bitRate>=100000&&c.bitRate<=200000000
        &&c.maxAccessUnitBytes>=1024&&c.maxAccessUnitBytes<=MaxAccessUnit;
}
bool validKey(const MaskHevcKey& k,const MaskHevcConfig& c)noexcept{
    return k.sourceContract&&k.encoderGeneration&&k.submission&&k.resourceGeneration&&k.mapGeneration&&k.processId
        &&k.width==c.width&&k.height==c.height;
}
bool validTable(const NV_ENCODE_API_FUNCTION_LIST& f)noexcept{
    return f.version==NV_ENCODE_API_FUNCTION_LIST_VER&&f.nvEncOpenEncodeSessionEx&&f.nvEncDestroyEncoder
        &&f.nvEncGetEncodeCaps&&f.nvEncGetInputFormatCount&&f.nvEncGetInputFormats&&f.nvEncGetEncodePresetConfigEx
        &&f.nvEncInitializeEncoder&&f.nvEncReconfigureEncoder&&f.nvEncCreateBitstreamBuffer&&f.nvEncDestroyBitstreamBuffer
        &&f.nvEncRegisterResource&&f.nvEncUnregisterResource&&f.nvEncMapInputResource&&f.nvEncUnmapInputResource
        &&f.nvEncRegisterAsyncEvent&&f.nvEncUnregisterAsyncEvent&&f.nvEncEncodePicture
        &&f.nvEncLockBitstream&&f.nvEncUnlockBitstream;
}
detail::MaskHevcPlatform platform()noexcept{
#if defined(SFNP_MASK_HEVC_HOST_TEST)
    return detail::maskHevcTestPlatform();
#elif defined(_WIN32)
    return {
        [](ID3D11Device* device,ID3D11Texture2D* texture,std::uint32_t width,std::uint32_t height)noexcept{
            if(!device||!texture)return false;
            D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
            if(d.Width!=width||d.Height!=height||d.Format!=DXGI_FORMAT_NV12||d.MipLevels!=1||d.ArraySize!=1
               ||d.SampleDesc.Count!=1||d.SampleDesc.Quality||d.Usage!=D3D11_USAGE_DEFAULT||d.CPUAccessFlags)return false;
            ID3D11Device* actual=nullptr;texture->GetDevice(&actual);if(!actual)return false;
            IUnknown* a=nullptr;IUnknown* b=nullptr;
            const bool same=SUCCEEDED(actual->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&a)))
                &&SUCCEEDED(device->QueryInterface(IID_IUnknown,reinterpret_cast<void**>(&b)))&&a==b;
            if(a)a->Release();
            if(b)b->Release();
            actual->Release();return same;
        },
        [](ID3D11Device* p)noexcept{p->AddRef();},[](ID3D11Device* p)noexcept{p->Release();},
        [](ID3D11Texture2D* p)noexcept{p->AddRef();},[](ID3D11Texture2D* p)noexcept{p->Release();},
        []()noexcept->void*{return CreateEventW(nullptr,FALSE,FALSE,nullptr);},
        [](void* h)noexcept{CloseHandle(h);},[](void* h)noexcept{return ResetEvent(h)!=FALSE;},
        [](void* h)noexcept{const auto r=WaitForSingleObject(h,0);return r==WAIT_OBJECT_0?1:(r==WAIT_TIMEOUT?0:-1);},
        [](void* h)noexcept{return WaitForSingleObject(h,2000)==WAIT_OBJECT_0;}
    };
#else
    return {};
#endif
}
}

struct MaskHevcEncoder::Impl {
    enum class Phase { Free,Staged,Submitted,Discard };
    struct Slot {
        ID3D11Texture2D* texture=nullptr;NV_ENC_REGISTERED_PTR registered=nullptr;NV_ENC_INPUT_PTR mapped=nullptr;
        NV_ENC_OUTPUT_PTR output=nullptr;void* event=nullptr;bool eventRegistered=false,notified=false,locked=false;
        Phase phase=Phase::Free;MaskHevcKey key{};std::uint64_t token=0,rateEpoch=0;
    };
    detail::MaskHevcPlatform os=platform();NV_ENCODE_API_FUNCTION_LIST api{};void* session=nullptr;
    ID3D11Device* device=nullptr;MaskHevcConfig config{};std::array<Slot,StageCount> slots{};
    void* flushEvent=nullptr;bool flushRegistered=false,initialized=false,submittedAny=false,flushStarted=false;
    std::uint64_t nextToken=1,sourceContract=0,encoderGeneration=0,highestSubmission=0;
    MaskHevcRate rate;std::uint64_t rateEpoch=0;unsigned rateBackoff=0;
    NV_ENC_INITIALIZE_PARAMS rateInit{};NV_ENC_CONFIG rateConfig{};
    std::atomic<std::uint32_t> rateTarget{0},rateBudget{0},streamTarget{0},rateNum{0},rateDen{0},wireMean{0};
    std::atomic<std::uint64_t> rateChanges{0};
    std::uint32_t processId=0;
    mutable std::mutex mutex;std::atomic<bool> healthy{false};std::atomic<int> status{int(NV_ENC_SUCCESS)};
    std::atomic<unsigned> failureStage{0},failureFields{0},outputType{0},outputStruct{0},hardwareStatus{0},outputBytes{0};
    std::atomic<int> failureStatus{0};
    std::atomic<std::uint64_t> expectedTimestamp{0},returnedTimestamp{0},mappedCount{0},submittedCount{0},lockedCount{0},readyCount{0};
    void record(MaskHevcFailure stage,NVENCSTATUS result,unsigned fields=0,
                const NV_ENC_LOCK_BITSTREAM* output=nullptr,std::uint64_t expected=0)noexcept{
        failureStatus.store(int(result),std::memory_order_relaxed);failureFields.store(fields,std::memory_order_relaxed);
        expectedTimestamp.store(expected,std::memory_order_relaxed);returnedTimestamp.store(output?output->outputTimeStamp:0,std::memory_order_relaxed);
        outputType.store(output?unsigned(output->pictureType):0,std::memory_order_relaxed);
        outputStruct.store(output?unsigned(output->pictureStruct):0,std::memory_order_relaxed);
        hardwareStatus.store(output?output->hwEncodeStatus:0,std::memory_order_relaxed);
        outputBytes.store(output?output->bitstreamSizeInBytes:0,std::memory_order_relaxed);
        failureStage.store(unsigned(stage),std::memory_order_relaxed);
    }
    bool success(NVENCSTATUS result,MaskHevcFailure stage=MaskHevcFailure::Setup)noexcept{
        if(result!=NV_ENC_SUCCESS){status.store(int(result),std::memory_order_relaxed);record(stage,result);}
        return result==NV_ENC_SUCCESS;
    }
    bool fail(NVENCSTATUS result,MaskHevcFailure stage=MaskHevcFailure::Setup)noexcept{
        status.store(int(result),std::memory_order_relaxed);record(stage,result);healthy.store(false,std::memory_order_release);return false;
    }
    Slot* find(const MaskHevcKey& key)noexcept{for(auto& s:slots)if(s.phase!=Phase::Free&&s.key==key)return &s;return nullptr;}
    Slot* find(ID3D11Texture2D* texture)noexcept{for(auto& s:slots)if(s.texture==texture)return &s;return nullptr;}
    bool notified(Slot& s)noexcept{
        if(s.notified)return true;
        const int result=os.pollEvent(s.event);if(result<0)return fail(NV_ENC_ERR_GENERIC,MaskHevcFailure::PollEvent);
        s.notified=result==1;return s.notified;
    }
    bool releaseMapped(Slot& s)noexcept{
        if(s.mapped){
            const auto result=api.nvEncUnmapInputResource(session,s.mapped);
            if(result!=NV_ENC_SUCCESS)return fail(result,MaskHevcFailure::UnmapInput);
            s.mapped=nullptr;
        }
        s.phase=Phase::Free;s.key={};s.token=0;s.rateEpoch=0;s.notified=false;return true;
    }
    // Called only after event notification. A canceled in-flight texture must
    // not be recycled merely because its native RGB frame has been retired.
    MaskHevcTake take(Slot& s,std::vector<std::uint8_t>* out,std::uint64_t* epoch=nullptr)noexcept{
        if(!notified(s))return healthy?MaskHevcTake::Pending:MaskHevcTake::Refused;
        NV_ENC_LOCK_BITSTREAM lock{};lock.version=NV_ENC_LOCK_BITSTREAM_VER;lock.outputBitstream=s.output;lock.doNotWait=1;
        const auto result=api.nvEncLockBitstream(session,&lock);status.store(int(result),std::memory_order_relaxed);
        if(result==NV_ENC_ERR_LOCK_BUSY||result==NV_ENC_ERR_ENCODER_BUSY)return MaskHevcTake::Pending;
        if(result!=NV_ENC_SUCCESS){fail(result,MaskHevcFailure::LockBitstream);return MaskHevcTake::Refused;}
        lockedCount.fetch_add(1,std::memory_order_relaxed);
        s.locked=true;
        // hwEncodeStatus is a completion state, not an NVENCSTATUS return code.
        // NVIDIA's S4654 documents 2 as complete. Keep the previously accepted
        // zero value too; partial/unknown states still fail closed. The exact
        // completion event, API success, token and bitstream checks remain required.
        const bool hardwareComplete=lock.hwEncodeStatus==0||lock.hwEncodeStatus==2;
        const unsigned rejected=(lock.outputTimeStamp!=s.token?1u:0u)|(lock.pictureType!=NV_ENC_PIC_TYPE_IDR?2u:0u)
            |(lock.pictureStruct!=NV_ENC_PIC_STRUCT_FRAME?4u:0u)|(!hardwareComplete?8u:0u)
            |(!lock.bitstreamBufferPtr?16u:0u)|(!lock.bitstreamSizeInBytes||lock.bitstreamSizeInBytes>config.maxAccessUnitBytes?32u:0u);
        const auto expected=s.token,encodedRateEpoch=s.rateEpoch;bool valid=rejected==0;
        if(valid&&out)try{
            const auto* bytes=static_cast<const std::uint8_t*>(lock.bitstreamBufferPtr);
            out->assign(bytes,bytes+lock.bitstreamSizeInBytes);
        }catch(...){valid=false;out->clear();}
        const auto unlocked=api.nvEncUnlockBitstream(session,s.output);
        if(unlocked!=NV_ENC_SUCCESS){if(out)out->clear();fail(unlocked,MaskHevcFailure::UnlockBitstream);return MaskHevcTake::Refused;}
        s.locked=false;
        if(!releaseMapped(s)){if(out)out->clear();return MaskHevcTake::Refused;}
        if(!valid){
            // A successful API call with rejected output is our validation
            // failure, never a fabricated NV_ENC_ERR_INVALID_PARAM from NVIDIA.
            record(MaskHevcFailure::OutputValidation,NV_ENC_SUCCESS,rejected?rejected:64u,&lock,expected);
            if(out)out->clear();
            return MaskHevcTake::Refused;
        }
        if(out){readyCount.fetch_add(1,std::memory_order_relaxed);if(epoch)*epoch=encodedRateEpoch;}
        return out?MaskHevcTake::Ready:MaskHevcTake::Refused;
    }
    // Cold and quiesced only. No OS/event wait is reachable from hot methods.
    bool clear()noexcept{
        healthy.store(false,std::memory_order_release);
        if(session&&initialized&&submittedAny&&flushRegistered){
            if(!flushStarted){
                if(!os.resetEvent(flushEvent))return fail(NV_ENC_ERR_GENERIC);
                NV_ENC_PIC_PARAMS eos{};eos.version=NV_ENC_PIC_PARAMS_VER;eos.encodePicFlags=NV_ENC_PIC_FLAG_EOS;eos.completionEvent=flushEvent;
                flushStarted=success(api.nvEncEncodePicture(session,&eos),MaskHevcFailure::Flush);
            }
            if(flushStarted){
                // Keep all handles and COM references alive if even the cold
                // flush deadline is missed. A retry waits on this SAME event.
                if(!os.waitEventCold(flushEvent))return fail(NV_ENC_ERR_LOCK_BUSY,MaskHevcFailure::FlushWait);
            }else{
                // Failed EOS is not a completion proof. Only individually
                // signaled accepted outputs may be released without it.
                for(auto& s:slots)if((s.phase==Phase::Submitted||s.phase==Phase::Discard)&&!notified(s))return false;
            }
        }
        for(auto& s:slots){
            if(session&&s.locked)api.nvEncUnlockBitstream(session,s.output);
            if(session&&s.mapped)api.nvEncUnmapInputResource(session,s.mapped);
            if(session&&s.registered)api.nvEncUnregisterResource(session,s.registered);
            if(session&&s.output)api.nvEncDestroyBitstreamBuffer(session,s.output);
            if(session&&s.eventRegistered){NV_ENC_EVENT_PARAMS e{};e.version=NV_ENC_EVENT_PARAMS_VER;e.completionEvent=s.event;api.nvEncUnregisterAsyncEvent(session,&e);}
            if(s.event)os.closeEvent(s.event);
            if(s.texture)os.releaseTexture(s.texture);
            s={};
        }
        if(session&&flushRegistered){NV_ENC_EVENT_PARAMS e{};e.version=NV_ENC_EVENT_PARAMS_VER;e.completionEvent=flushEvent;api.nvEncUnregisterAsyncEvent(session,&e);}
        if(flushEvent)os.closeEvent(flushEvent);
        if(session)api.nvEncDestroyEncoder(session);
        if(device)os.releaseDevice(device);
        api={};session=nullptr;device=nullptr;config={};flushEvent=nullptr;rate={};rateInit={};rateConfig={};rateBackoff=0;
        flushRegistered=initialized=submittedAny=flushStarted=false;nextToken=1;sourceContract=encoderGeneration=highestSubmission=0;processId=0;
        return true;
    }
    bool configure(ID3D11Device* d,const MaskHevcConfig& c,const std::array<ID3D11Texture2D*,StageCount>& textures,
                   const NV_ENCODE_API_FUNCTION_LIST& f)noexcept{
        if(!d||!os.textureMatches||!validConfig(c)||!validTable(f))return false;
        for(unsigned i=0;i<StageCount;++i){
            if(!os.textureMatches(d,textures[i],c.width,c.height))return false;
            for(unsigned j=0;j<i;++j)if(textures[i]==textures[j])return false;
        }
        api=f;config=c;device=d;os.retainDevice(device);
        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};open.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
        open.device=device;open.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;open.apiVersion=NVENCAPI_VERSION;
        if(!success(api.nvEncOpenEncodeSessionEx(&open,&session))||!session)return false;
        for(const auto cap:{NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT,NV_ENC_CAPS_WIDTH_MAX,NV_ENC_CAPS_HEIGHT_MAX}){
            NV_ENC_CAPS_PARAM p{};p.version=NV_ENC_CAPS_PARAM_VER;p.capsToQuery=cap;int supported=0;
            if(!success(api.nvEncGetEncodeCaps(session,NV_ENC_CODEC_HEVC_GUID,&p,&supported)))return false;
            const auto required=cap==NV_ENC_CAPS_WIDTH_MAX?c.width:(cap==NV_ENC_CAPS_HEIGHT_MAX?c.height:1u);
            if(supported<=0||std::uint32_t(supported)<required)return false;
        }
        std::uint32_t count=0,actual=0;std::array<NV_ENC_BUFFER_FORMAT,32> formats{};
        if(!success(api.nvEncGetInputFormatCount(session,NV_ENC_CODEC_HEVC_GUID,&count))||!count||count>formats.size()
           ||!success(api.nvEncGetInputFormats(session,NV_ENC_CODEC_HEVC_GUID,formats.data(),count,&actual))||!actual||actual>count)return false;
        bool nv12=false;for(unsigned i=0;i<actual;++i)nv12|=formats[i]==NV_ENC_BUFFER_FORMAT_NV12;if(!nv12)return false;
        NV_ENC_PRESET_CONFIG preset{};preset.version=NV_ENC_PRESET_CONFIG_VER;preset.presetCfg.version=NV_ENC_CONFIG_VER;
        if(!success(api.nvEncGetEncodePresetConfigEx(session,NV_ENC_CODEC_HEVC_GUID,NV_ENC_PRESET_P1_GUID,NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,&preset)))return false;
        const auto planned=rate.initial(c.bitRate,c.frameRateNum,c.frameRateDen,c.streamBitRate);
        NV_ENC_CONFIG enc=preset.presetCfg;enc.version=NV_ENC_CONFIG_VER;enc.profileGUID=NV_ENC_HEVC_PROFILE_MAIN_GUID;
        enc.gopLength=1;enc.frameIntervalP=1;enc.monoChromeEncoding=0;enc.frameFieldMode=NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
        enc.rcParams={};enc.rcParams.version=NV_ENC_RC_PARAMS_VER;enc.rcParams.rateControlMode=NV_ENC_PARAMS_RC_VBR;
        enc.rcParams.averageBitRate=enc.rcParams.maxBitRate=planned.target;
        enc.rcParams.vbvBufferSize=std::uint32_t((std::uint64_t(planned.target)*c.frameRateDen+c.frameRateNum-1)/c.frameRateNum);
        enc.rcParams.vbvInitialDelay=enc.rcParams.vbvBufferSize;enc.rcParams.zeroReorderDelay=1;
        enc.rcParams.lowDelayKeyFrameScale=1;enc.rcParams.multiPass=NV_ENC_MULTI_PASS_DISABLED;
        enc.encodeCodecConfig={};auto& hevc=enc.encodeCodecConfig.hevcConfig;
        hevc.chromaFormatIDC=1;hevc.pixelBitDepthMinus8=0;hevc.idrPeriod=1;hevc.maxNumRefFramesInDPB=1;
        // The signed per-mask payload already delimits one complete IDR. The
        // private decoder needs VPS/SPS/PPS, not optional access-unit delimiters.
        hevc.repeatSPSPPS=1;hevc.outputAUD=0;hevc.sliceMode=0;hevc.sliceModeData=0;
        hevc.hevcVUIParameters.videoSignalTypePresentFlag=1;hevc.hevcVUIParameters.videoFormat=5;
        hevc.hevcVUIParameters.videoFullRangeFlag=1;hevc.hevcVUIParameters.colourDescriptionPresentFlag=0;
        NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;init.encodeGUID=NV_ENC_CODEC_HEVC_GUID;
        init.presetGUID=NV_ENC_PRESET_P1_GUID;init.encodeWidth=init.darWidth=init.maxEncodeWidth=c.width;
        init.encodeHeight=init.darHeight=init.maxEncodeHeight=c.height;init.frameRateNum=c.frameRateNum;init.frameRateDen=c.frameRateDen;
        init.enableEncodeAsync=1;init.enablePTD=1;init.tuningInfo=NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;init.encodeConfig=&enc;
        if(!success(api.nvEncInitializeEncoder(session,&init)))return false;
        initialized=true;rateConfig=enc;rateInit=init;rateInit.encodeConfig=&rateConfig;
        if(rateEpoch==UINT64_MAX)return false;
        ++rateEpoch;rateTarget=planned.target;rateBudget=planned.cap;streamTarget=c.streamBitRate;rateNum=c.frameRateNum;rateDen=c.frameRateDen;wireMean=0;
        flushEvent=os.createEvent();if(!flushEvent)return false;
        NV_ENC_EVENT_PARAMS event{};event.version=NV_ENC_EVENT_PARAMS_VER;event.completionEvent=flushEvent;
        if(!success(api.nvEncRegisterAsyncEvent(session,&event)))return false;
        flushRegistered=true;
        for(unsigned i=0;i<StageCount;++i){
            auto& s=slots[i];s.texture=textures[i];os.retainTexture(s.texture);
            NV_ENC_REGISTER_RESOURCE resource{};resource.version=NV_ENC_REGISTER_RESOURCE_VER;
            resource.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;resource.resourceToRegister=s.texture;
            resource.width=c.width;resource.height=c.height;resource.bufferFormat=NV_ENC_BUFFER_FORMAT_NV12;resource.bufferUsage=NV_ENC_INPUT_IMAGE;
            if(!success(api.nvEncRegisterResource(session,&resource))||!resource.registeredResource)return false;
            s.registered=resource.registeredResource;
            NV_ENC_CREATE_BITSTREAM_BUFFER output{};output.version=NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
            if(!success(api.nvEncCreateBitstreamBuffer(session,&output))||!output.bitstreamBuffer)return false;
            s.output=output.bitstreamBuffer;s.event=os.createEvent();if(!s.event)return false;
            event.completionEvent=s.event;if(!success(api.nvEncRegisterAsyncEvent(session,&event)))return false;
            s.eventRegistered=true;
        }
        healthy.store(true,std::memory_order_release);return true;
    }
};

MaskHevcEncoder::MaskHevcEncoder():impl_(std::make_unique<Impl>()){}
MaskHevcEncoder::~MaskHevcEncoder(){
    // A catastrophically hung driver must not outlive freed GPU resources.
    // At most this one bounded session is quarantined until process exit;
    // configure/reset never allocate replacements while its flush is pending.
    if(!reset())(void)impl_.release();
}
bool MaskHevcEncoder::configure(ID3D11Device* device,const MaskHevcConfig& config,
                              const std::array<ID3D11Texture2D*,StageCount>& textures,const NV_ENCODE_API_FUNCTION_LIST& original)noexcept{
    std::lock_guard<std::mutex> guard(impl_->mutex);if(!impl_->clear())return false;
    impl_->status.store(int(NV_ENC_SUCCESS),std::memory_order_relaxed);
    if(impl_->configure(device,config,textures,original))return true;
    if(impl_->status.load(std::memory_order_relaxed)==int(NV_ENC_SUCCESS))impl_->status.store(int(NV_ENC_ERR_UNSUPPORTED_PARAM),std::memory_order_relaxed);
    impl_->clear();return false;
}
bool MaskHevcEncoder::canStage(ID3D11Texture2D* texture)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);if(!guard||!p.healthy)return false;
    auto* s=p.find(texture);if(!s)return false;
    // The caller's FREE/RETIRED reuse claim retires the old native frame even
    // if an earlier best-effort cancel lost a try-lock race. Never free an
    // accepted GPU input until its own completion event is observed.
    if(s->phase==Impl::Phase::Staged)p.releaseMapped(*s);
    if(s->phase==Impl::Phase::Submitted)s->phase=Impl::Phase::Discard;
    if(s->phase==Impl::Phase::Discard)p.take(*s,nullptr);
    return p.healthy&&s->phase==Impl::Phase::Free;
}
bool MaskHevcEncoder::beginStage(const MaskHevcKey& key,ID3D11Texture2D* texture)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);if(!guard||!p.healthy||!validKey(key,p.config))return false;
    auto* s=p.find(texture);if(!s||s->phase!=Impl::Phase::Free||p.find(key)||key.submission<=p.highestSubmission
        ||p.nextToken>std::numeric_limits<std::uint32_t>::max())return false;
    if(p.sourceContract&&(p.sourceContract!=key.sourceContract||p.processId!=key.processId||p.encoderGeneration!=key.encoderGeneration))return false;
    if(!p.os.resetEvent(s->event))return p.fail(NV_ENC_ERR_GENERIC);
    p.sourceContract=key.sourceContract;p.processId=key.processId;p.encoderGeneration=key.encoderGeneration;p.highestSubmission=key.submission;
    s->key=key;s->token=p.nextToken++;s->notified=false;s->phase=Impl::Phase::Staged;return true;
}
bool MaskHevcEncoder::submit(const MaskHevcKey& key)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);if(!guard||!p.healthy)return false;
    auto* s=p.find(key);if(!s||s->phase!=Impl::Phase::Staged)return false;
    NV_ENC_MAP_INPUT_RESOURCE map{};map.version=NV_ENC_MAP_INPUT_RESOURCE_VER;map.registeredResource=s->registered;
    const auto mapped=p.api.nvEncMapInputResource(p.session,&map);
    if(!p.success(mapped,MaskHevcFailure::MapInput)||!map.mappedResource||map.mappedBufferFmt!=NV_ENC_BUFFER_FORMAT_NV12){
        if(mapped==NV_ENC_SUCCESS)p.record(MaskHevcFailure::MapOutput,mapped);
        if(map.mappedResource)s->mapped=map.mappedResource;
        p.releaseMapped(*s);return false;
    }
    s->mapped=map.mappedResource;
    p.mappedCount.fetch_add(1,std::memory_order_relaxed);
    NV_ENC_PIC_PARAMS pic{};pic.version=NV_ENC_PIC_PARAMS_VER;pic.inputWidth=p.config.width;pic.inputHeight=p.config.height;
    pic.bufferFmt=NV_ENC_BUFFER_FORMAT_NV12;pic.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;pic.inputBuffer=s->mapped;
    pic.outputBitstream=s->output;pic.completionEvent=s->event;pic.inputTimeStamp=s->token;pic.frameIdx=std::uint32_t(s->token);
    pic.encodePicFlags=NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    const auto result=p.api.nvEncEncodePicture(p.session,&pic);p.status.store(int(result),std::memory_order_relaxed);
    if(result!=NV_ENC_SUCCESS&&result!=NV_ENC_ERR_NEED_MORE_INPUT){p.record(MaskHevcFailure::EncodePicture,result);p.releaseMapped(*s);return false;}
    p.submittedCount.fetch_add(1,std::memory_order_relaxed);
    s->rateEpoch=p.rateEpoch;s->phase=Impl::Phase::Submitted;p.submittedAny=true;return true;
}
MaskHevcTake MaskHevcEncoder::tryTake(const MaskHevcKey& key,std::vector<std::uint8_t>& out,std::uint64_t* rateEpoch)noexcept{
    out.clear();if(rateEpoch)*rateEpoch=0;auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);
    if(!guard)return MaskHevcTake::Busy;
    if(!p.healthy)return MaskHevcTake::Unavailable;
    auto* s=p.find(key);if(!s||s->phase!=Impl::Phase::Submitted)return MaskHevcTake::Missing;
    return p.take(*s,&out,rateEpoch);
}
bool MaskHevcEncoder::updateRate(std::uint32_t num,std::uint32_t den,std::uint32_t stream,std::uint32_t user)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);
    if(!guard||!p.healthy||p.rateBackoff||p.rateEpoch==UINT64_MAX)return false;
    auto config=p.config;config.frameRateNum=num;config.frameRateDen=den;config.bitRate=user;config.streamBitRate=stream;
    if(!validConfig(config))return false;
    const auto plan=p.rate.plan(user,num,den,stream);p.rateBudget=plan.cap;p.streamTarget=stream;
    if(!plan.change)return true;
    // Dynamic RC is serialized with Encode, not with GPU completion. Waiting
    // for every slot to empty could starve updates during continuous playback.
    // Each accepted submission retains its own rate epoch for byte feedback.
    NV_ENC_CONFIG enc=p.rateConfig;enc.rcParams.averageBitRate=enc.rcParams.maxBitRate=plan.target;
    enc.rcParams.vbvBufferSize=std::uint32_t((std::uint64_t(plan.target)*plan.den+plan.num-1)/plan.num);
    enc.rcParams.vbvInitialDelay=enc.rcParams.vbvBufferSize;
    NV_ENC_RECONFIGURE_PARAMS params{};params.version=NV_ENC_RECONFIGURE_PARAMS_VER;params.reInitEncodeParams=p.rateInit;
    params.reInitEncodeParams.frameRateNum=plan.num;params.reInitEncodeParams.frameRateDen=plan.den;params.reInitEncodeParams.encodeConfig=&enc;
    const auto result=p.api.nvEncReconfigureEncoder(p.session,&params);p.status.store(int(result),std::memory_order_relaxed);
    if(result!=NV_ENC_SUCCESS){p.record(MaskHevcFailure::Reconfigure,result);p.rateBackoff=MaskHevcRate::Window;return false;}
    p.rateConfig=enc;p.rateInit=params.reInitEncodeParams;p.rateInit.encodeConfig=&p.rateConfig;p.config=config;
    p.rate.commit(plan);++p.rateEpoch;p.rateTarget=plan.target;p.rateNum=plan.num;p.rateDen=plan.den;p.wireMean=0;
    p.rateChanges.fetch_add(1,std::memory_order_relaxed);return true;
}
void MaskHevcEncoder::observeAddedBytes(std::uint64_t epoch,std::size_t bytes)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);
    if(!guard||!p.healthy||!epoch||epoch!=p.rateEpoch)return;
    p.rate.observe(bytes);const auto current=p.rate.current();p.wireMean=p.rate.observed(current.num,current.den);
    if(p.rateBackoff)--p.rateBackoff;
}

void MaskHevcEncoder::cancel(const MaskHevcKey& key)noexcept{
    auto& p=*impl_;std::unique_lock<std::mutex> guard(p.mutex,std::try_to_lock);if(!guard||!p.healthy)return;
    auto* s=p.find(key);if(!s)return;
    if(s->phase==Impl::Phase::Staged){p.releaseMapped(*s);return;}
    s->phase=Impl::Phase::Discard;p.take(*s,nullptr);
}
bool MaskHevcEncoder::reset()noexcept{std::lock_guard<std::mutex> guard(impl_->mutex);return impl_->clear();}
bool MaskHevcEncoder::configured()const noexcept{return impl_->healthy.load(std::memory_order_acquire);}
int MaskHevcEncoder::lastNvencStatus()const noexcept{return impl_->status.load(std::memory_order_relaxed);}
MaskHevcDiagnostics MaskHevcEncoder::diagnostics()const noexcept{
    const auto& p=*impl_;MaskHevcDiagnostics d{};
    d.stage=static_cast<MaskHevcFailure>(p.failureStage.load(std::memory_order_relaxed));d.nativeStatus=p.failureStatus.load(std::memory_order_relaxed);
    d.rejectedFields=p.failureFields.load(std::memory_order_relaxed);d.pictureType=p.outputType.load(std::memory_order_relaxed);
    d.pictureStruct=p.outputStruct.load(std::memory_order_relaxed);d.hardwareStatus=p.hardwareStatus.load(std::memory_order_relaxed);
    d.bytes=p.outputBytes.load(std::memory_order_relaxed);d.expectedTimestamp=p.expectedTimestamp.load(std::memory_order_relaxed);
    d.returnedTimestamp=p.returnedTimestamp.load(std::memory_order_relaxed);d.mapped=p.mappedCount.load(std::memory_order_relaxed);
    d.submitted=p.submittedCount.load(std::memory_order_relaxed);d.locked=p.lockedCount.load(std::memory_order_relaxed);d.ready=p.readyCount.load(std::memory_order_relaxed);
    d.rateTarget=p.rateTarget.load(std::memory_order_relaxed);d.rateBudget=p.rateBudget.load(std::memory_order_relaxed);d.streamTarget=p.streamTarget.load(std::memory_order_relaxed);
    d.frameRateNum=p.rateNum.load(std::memory_order_relaxed);d.frameRateDen=p.rateDen.load(std::memory_order_relaxed);d.wireMean=p.wireMean.load(std::memory_order_relaxed);d.rateChanges=p.rateChanges.load(std::memory_order_relaxed);return d;
}
}
