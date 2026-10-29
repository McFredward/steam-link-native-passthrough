// Offline, owned synthetic-data diagnostic. Nothing here is linked into the
// installed provider. Deliberate API overrides are labelled per comparison;
// P outputs remain REFUSED by the unchanged production IDR guard.
#include "probe_pattern.h"
#include "pc/stream-bridge-mask-hevc.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mmsystem.h>
#include <nvEncodeAPI.h>
#ifndef SFNP_PROBE_SOURCE_REVISION
#define SFNP_PROBE_SOURCE_REVISION "development"
#endif
namespace {
using namespace sfnp_probe;
using stream_bridge::sender::MaskHevcEncoder;
using stream_bridge::sender::MaskHevcConfig;
using stream_bridge::sender::MaskHevcKey;
using stream_bridge::sender::MaskHevcTake;
template<class T>struct Com {
    T* value=nullptr;~Com(){if(value)value->Release();}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
    T** put(){if(value){value->Release();value=nullptr;}return &value;}
    T* operator->()const{return value;}
};
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void checkHr(HRESULT hr,const char* message){if(FAILED(hr)){std::cerr<<message<<": HRESULT=0x"<<std::hex<<unsigned(hr)<<std::dec<<'\n';throw std::runtime_error(message);}}
void save(const std::filesystem::path& path,const void* bytes,std::size_t size){
    std::ofstream out(path,std::ios::binary);out.write(static_cast<const char*>(bytes),std::streamsize(size));require(bool(out),"Cannot save diagnostic output");
}
struct Mode {const char* name;unsigned target;bool dynamic=false,automatic=false,p3=false,multipass=false,predicted=false,longGop=false;};
constexpr Mode Modes[]{
    {"production-vbr-20m",20000000},
    {"fresh-vbr-1m",1000000},
    {"fresh-vbr-100k",100000},
    {"dynamic-vbr-20m-to-100k",20000000,true},
    {"automatic-idr-vbr-100k",100000,false,true},
    {"automatic-idr-vbr-20m",20000000,false,true},
    {"infinite-gop-forced-idr-100k",100000,false,false,false,false,false,true},
    {"infinite-gop-forced-idr-20m",20000000,false,false,false,false,false,true},
    {"p3-qres-vbr-20m",20000000,false,false,true,true},
    {"p3-qres-ippp8-vbr-20m",20000000,false,false,true,true,true},
};
struct Observation {bool present=false;unsigned bytes=0,qp=0,type=0;std::uint64_t timestamp=0;bool validMetadata=false;};
struct Port {
    NV_ENCODE_API_FUNCTION_LIST real{};const Mode* mode=nullptr;void* session=nullptr;
    NV_ENC_INITIALIZE_PARAMS init{};NV_ENC_CONFIG config{};Observation last{};
    std::vector<std::uint8_t> prefix;bool prefixHasKey=false;unsigned prefixFrames=0;
    std::ofstream* trace=nullptr;unsigned frame=0;
} *active=nullptr;
NVENCSTATUS NVENCAPI open(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS* p,void** session){
    const auto result=active->real.nvEncOpenEncodeSessionEx(p,session);if(result==NV_ENC_SUCCESS)active->session=*session;return result;
}
NVENCSTATUS NVENCAPI preset(void* session,GUID codec,GUID guid,NV_ENC_TUNING_INFO tuning,NV_ENC_PRESET_CONFIG* out){
    return active->real.nvEncGetEncodePresetConfigEx(session,codec,active->mode->p3?NV_ENC_PRESET_P3_GUID:guid,tuning,out);
}
NVENCSTATUS NVENCAPI initialize(void* session,NV_ENC_INITIALIZE_PARAMS* input){
    auto& p=*active;p.init=*input;p.config=*input->encodeConfig;p.init.encodeConfig=&p.config;
    if(p.mode->p3)p.init.presetGUID=NV_ENC_PRESET_P3_GUID;
    if(p.mode->multipass)p.config.rcParams.multiPass=NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
    if(p.mode->longGop){p.config.gopLength=NVENC_INFINITE_GOPLENGTH;p.config.encodeCodecConfig.hevcConfig.idrPeriod=NVENC_INFINITE_GOPLENGTH;}
    if(p.mode->predicted){p.config.gopLength=8;p.config.encodeCodecConfig.hevcConfig.idrPeriod=8;}
    return p.real.nvEncInitializeEncoder(session,&p.init);
}
NVENCSTATUS NVENCAPI encode(void* session,NV_ENC_PIC_PARAMS* input){
    if(input->encodePicFlags&NV_ENC_PIC_FLAG_EOS)return active->real.nvEncEncodePicture(session,input);
    auto copy=*input;
    if(active->mode->automatic)copy.encodePicFlags=NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    if(active->mode->predicted)copy.encodePicFlags=active->frame%8?0:NV_ENC_PIC_FLAG_FORCEIDR|NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
    return active->real.nvEncEncodePicture(session,&copy);
}
NVENCSTATUS NVENCAPI lock(void* session,NV_ENC_LOCK_BITSTREAM* output){
    const auto status=active->real.nvEncLockBitstream(session,output);
    if(status!=NV_ENC_SUCCESS)return status;
    auto& p=*active;
    // This is an offline API observation, not a certified/current image. Copy
    // the diagnostic before the production guard rejects a comparison P frame.
    p.last={true,output->bitstreamSizeInBytes,output->frameAvgQP,unsigned(output->pictureType),output->outputTimeStamp,output->bitstreamBufferPtr&&output->pictureStruct==NV_ENC_PIC_STRUCT_FRAME&&(output->hwEncodeStatus==0||output->hwEncodeStatus==2)};
    if(output->bitstreamBufferPtr&&output->bitstreamSizeInBytes&&output->bitstreamSizeInBytes<=2u*1024u*1024u){
        if(output->pictureType==NV_ENC_PIC_TYPE_IDR){p.prefix.clear();p.prefixHasKey=true;p.prefixFrames=0;}
        if(p.prefixHasKey&&p.prefixFrames<16&&p.prefix.size()+output->bitstreamSizeInBytes<=8u*1024u*1024u){
            const auto* bytes=static_cast<const std::uint8_t*>(output->bitstreamBufferPtr);try{p.prefix.insert(p.prefix.end(),bytes,bytes+output->bitstreamSizeInBytes);++p.prefixFrames;}catch(...){p.prefixHasKey=false;}
        }else p.prefixHasKey=false;
    }
    return status;
}
struct Stats {std::uint64_t bytes=0,qp=0;unsigned frames=0,minQp=UINT32_MAX,maxQp=0,maxBytes=0;
    void add(const Observation& p){bytes+=p.bytes;qp+=p.qp;++frames;minQp=std::min(minQp,p.qp);maxQp=std::max(maxQp,p.qp);maxBytes=std::max(maxBytes,p.bytes);}
};
void fields(std::ostream& out,const Stats& s){out<<"{\"frames\":"<<s.frames<<",\"bytes\":"<<s.bytes<<",\"nominal_bitrate\":"<<(s.frames?s.bytes*8*Fps/s.frames:0)<<",\"QP_min\":"<<s.minQp<<",\"QP_mean\":"<<(s.frames?double(s.qp)/s.frames:0)<<",\"QP_max\":"<<s.maxQp<<",\"max_access_unit_bytes\":"<<s.maxBytes<<'}';}
bool runMode(const Mode& mode,ID3D11Device* device,ID3D11DeviceContext* context,const NV_ENCODE_API_FUNCTION_LIST& api,const std::filesystem::path& directory,std::ostream& summary){
    Port port;port.real=api;port.mode=&mode;active=&port;
    Com<ID3D11Texture2D> textures[4];std::array<ID3D11Texture2D*,4> raw{};
    for(unsigned i=0;i<4;++i){D3D11_TEXTURE2D_DESC desc{};desc.Width=Width;desc.Height=Height;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_NV12;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        checkHr(device->CreateTexture2D(&desc,nullptr,textures[i].put()),"Create fixed NV12 test texture");raw[i]=textures[i].value;}
    Com<ID3D11Texture2D> staging;
    {D3D11_TEXTURE2D_DESC desc{};raw[0]->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        checkHr(device->CreateTexture2D(&desc,nullptr,staging.put()),"Create owned input verification readback");}
    auto wrapped=api;wrapped.nvEncOpenEncodeSessionEx=open;wrapped.nvEncGetEncodePresetConfigEx=preset;wrapped.nvEncInitializeEncoder=initialize;wrapped.nvEncEncodePicture=encode;wrapped.nvEncLockBitstream=lock;
    MaskHevcEncoder encoder;const MaskHevcConfig config{Width,Height,Fps,1,mode.target,2u*1024u*1024u};
    const bool configured=encoder.configure(device,config,raw,wrapped);
    summary<<"{\"case\":\""<<mode.name<<"\",\"configured\":"<<(configured?"true":"false")<<",\"api_status\":"<<encoder.lastNvencStatus();
    if(!configured){summary<<",\"result\":\"unsupported-configuration\"}";std::cout<<mode.name<<": unsupported configuration, status="<<encoder.lastNvencStatus()<<std::endl;return false;}
    std::vector<std::uint8_t> pixels,output;std::ofstream trace(directory/(std::string(mode.name)+".jsonl"));require(bool(trace),"Cannot create trace");
    Stats all,steady,before,after;unsigned pFrames=0,intraGuardRefusals=0;int dynamicStatus=-1;
    for(unsigned frame=0;frame<Frames;++frame){
        port.frame=frame;port.last={};
        if(mode.dynamic&&frame==Frames/2){auto reinit=port.init;auto rate=port.config;rate.rcParams.averageBitRate=rate.rcParams.maxBitRate=100000;rate.rcParams.vbvBufferSize=rate.rcParams.vbvInitialDelay=(100000+Fps-1)/Fps;reinit.encodeConfig=&rate;
            NV_ENC_RECONFIGURE_PARAMS params{};params.version=NV_ENC_RECONFIGURE_PARAMS_VER;params.reInitEncodeParams=reinit;params.resetEncoder=1;dynamicStatus=int(api.nvEncReconfigureEncoder(port.session,&params));}
        pattern(frame,pixels);const auto index=frame%4;require(encoder.canStage(raw[index]),"A prior GPU input has not retired");
        context->UpdateSubresource(raw[index],0,nullptr,pixels.data(),Width,Width*Height*3/2);
        // Verify every generated Y/UV byte reached the actual GPU resource.
        // This cold diagnostic readback is excluded from submit-to-poll timing.
        context->CopyResource(staging.value,raw[index]);D3D11_MAPPED_SUBRESOURCE mapped{};
        checkHr(context->Map(staging.value,0,D3D11_MAP_READ,0,&mapped),"Verify uploaded NV12 input");
        bool identical=mapped.pData&&mapped.RowPitch>=Width&&std::uint64_t(mapped.RowPitch)*Height*3/2<=64u*1024u*1024u;
        if(identical)for(unsigned row=0;row<Height*3/2;++row)if(std::memcmp(static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(row)*mapped.RowPitch,pixels.data()+std::size_t(row)*Width,Width)){identical=false;break;}
        context->Unmap(staging.value,0);require(identical,"GPU NV12 bytes differ from synthetic source");
        const MaskHevcKey key{1,1,frame+1,1,1,frame,GetCurrentProcessId(),Width,Height};
        const auto begin=std::chrono::steady_clock::now();require(encoder.beginStage(key,raw[index])&&encoder.submit(key),"Private encode submission failed");
        MaskHevcTake result=MaskHevcTake::Pending;
        while(result==MaskHevcTake::Pending||result==MaskHevcTake::Busy){result=encoder.tryTake(key,output);if(result==MaskHevcTake::Pending||result==MaskHevcTake::Busy){require(std::chrono::steady_clock::now()-begin<std::chrono::seconds(5),"NVENC completion timed out");Sleep(1);}}
        require(port.last.present&&port.last.validMetadata&&port.last.timestamp==frame+1&&port.last.bytes&&port.last.bytes<=2u*1024u*1024u,"No complete exactly timestamped native output");
        if(result!=MaskHevcTake::Ready){require((mode.predicted||mode.automatic||mode.longGop||mode.p3)&&result==MaskHevcTake::Refused&&(port.last.type==NV_ENC_PIC_TYPE_P||port.last.type==NV_ENC_PIC_TYPE_I),"Production output validation refused a non-comparison output");++intraGuardRefusals;}
        if(port.last.type==NV_ENC_PIC_TYPE_P)++pFrames;
        require(port.last.qp<=51,"Driver reported unknown QP; preserve prior traces for analysis");
        all.add(port.last);if(frame>=32)steady.add(port.last);if(frame>=16&&frame<48)before.add(port.last);if(frame>=64)after.add(port.last);
        const auto age=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
        trace<<"{\"frame\":"<<frame<<",\"bytes\":"<<port.last.bytes<<",\"QP\":"<<port.last.qp<<",\"picture_type\":"<<port.last.type<<",\"submit_to_poll_us\":"<<age<<",\"frontend_ready\":"<<(result==MaskHevcTake::Ready?"true":"false")<<"}\n";
    }
    if(port.prefixHasKey){save(directory/(std::string(mode.name)+"-last-gop.hevc"),port.prefix.data(),port.prefix.size());}
    if(!std::filesystem::exists(directory/"reference-last.r8"))save(directory/"reference-last.r8",pixels.data(),std::size_t(Width)*Height);
    require(encoder.reset(),"Diagnostic NVENC flush did not complete");
    summary<<",\"verified_uploaded_frames\":"<<Frames<<",\"requested_initial_bitrate\":"<<mode.target<<",\"gop_length\":"<<port.config.gopLength<<",\"multipass\":"<<unsigned(port.config.rcParams.multiPass)<<",\"all\":";fields(summary,all);summary<<",\"steady\":";fields(summary,steady);summary<<",\"before_dynamic\":";fields(summary,before);summary<<",\"after_dynamic\":";fields(summary,after);
    summary<<",\"dynamic_reconfigure_status\":"<<dynamicStatus<<",\"diagnostic_P_frames\":"<<pFrames<<",\"production_intra_guard_refusals\":"<<intraGuardRefusals<<",\"last_gop_frames\":"<<port.prefixFrames<<",\"last_gop_captured\":"<<(port.prefixHasKey?"true":"false")<<",\"result\":\"complete\"}";
    std::cout<<mode.name<<": nominal "<<(steady.bytes*8*Fps/steady.frames)<<" bit/s, QP "<<double(steady.qp)/steady.frames<<", diagnostic P frames "<<pFrames<<std::endl;return true;
}
int selftest(){std::vector<std::uint8_t> bytes;pattern(0,bytes);const auto first=fingerprint(bytes);pattern(95,bytes);const auto last=fingerprint(bytes);std::cout<<"pattern_fingerprint="<<std::hex<<first<<'/'<<last<<std::dec<<"; bytes="<<bytes.size()<<'\n';return 0;}
} // namespace
int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--selftest")return selftest();
    if(argc!=3||std::wstring(argv[1])!=L"--output"){std::cerr<<"Usage: sfnp-nvenc-quality-probe --output DIRECTORY\n";return 2;}
    try{
        const std::filesystem::path directory(argv[2]);require(std::filesystem::is_directory(directory),"Output directory must already exist");
        Com<IDXGIFactory1> factory;checkHr(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(factory.put())),"Create DXGI factory");
        Com<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 description{};
        for(unsigned i=0;;++i){Com<IDXGIAdapter1> candidate;const auto status=factory->EnumAdapters1(i,candidate.put());require(status!=DXGI_ERROR_NOT_FOUND,"No NVIDIA D3D11 adapter found");checkHr(status,"Enumerate DXGI adapter");checkHr(candidate->GetDesc1(&description),"Read DXGI adapter");if(description.VendorId==0x10de){adapter.value=candidate.value;candidate.value=nullptr;break;}}
        Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};checkHr(D3D11CreateDevice(adapter.value,D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.put(),&level,context.put()),"Create NVIDIA D3D11 device");
        HMODULE library=LoadLibraryExW(L"nvEncodeAPI64.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);require(library!=nullptr,"NVIDIA System32 NVENC library unavailable");
        using Create=NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);Create create=nullptr;const auto symbol=GetProcAddress(library,"NvEncodeAPICreateInstance");static_assert(sizeof(create)==sizeof(symbol),"Windows function-pointer ABI");std::memcpy(&create,&symbol,sizeof create);require(create!=nullptr,"NVENC factory unavailable");
        NV_ENCODE_API_FUNCTION_LIST api{};api.version=NV_ENCODE_API_FUNCTION_LIST_VER;require(create(&api)==NV_ENC_SUCCESS,"NVIDIA does not accept pinned SDK11.1 ABI");
        // A header carries source/probe identity even if a later case fails.
        std::ofstream summary(directory/"probe.json");require(bool(summary),"Cannot create report");
        summary<<"{\"schema\":\"sfnp-nvenc-quality-probe-v1\",\"source_revision\":\""<<SFNP_PROBE_SOURCE_REVISION<<"\",\"width\":"<<Width<<",\"height\":"<<Height<<",\"frames\":"<<Frames<<",\"nominal_fps\":"<<Fps<<",\"adapter_vendor\":"<<description.VendorId<<",\"adapter_device\":"<<description.DeviceId<<",\"synthetic_input\":true,\"gpu_execution\":true,\"live_runtime_install_or_restart\":false,\"cases\":[";
        struct Timer {bool changed=timeBeginPeriod(1)==TIMERR_NOERROR;~Timer(){if(changed)timeEndPeriod(1);}} timer;
        unsigned completed=0,successful=0;bool failed=false;
        for(unsigned i=0;i<std::size(Modes);++i){
            std::ostringstream item;
            try{if(runMode(Modes[i],device.value,context.value,api,directory,item))++successful;}
            catch(const std::exception& error){std::cerr<<Modes[i].name<<": "<<error.what()<<std::endl;failed=true;
                item.str("");item<<"{\"case\":\""<<Modes[i].name<<"\",\"result\":\"execution-failed\"}";}
            if(i)summary<<',';
            summary<<item.str();summary.flush();if(failed)break;++completed;
        }
        failed=failed||!successful;summary<<"],\"completed_cases\":"<<completed<<",\"successful_cases\":"<<successful<<",\"execution_failed\":"<<(failed?"true":"false")<<"}\n";summary.close();
        if(!failed)FreeLibrary(library);
        return failed?1:0;
    }catch(const std::exception& error){std::cerr<<"Probe failed: "<<error.what()<<'\n';return 1;}
}
#else
int main(){std::vector<std::uint8_t> bytes;sfnp_probe::pattern(0,bytes);const auto first=sfnp_probe::fingerprint(bytes);sfnp_probe::pattern(95,bytes);const auto last=sfnp_probe::fingerprint(bytes);std::cout<<"pattern_fingerprint="<<std::hex<<first<<'/'<<last<<std::dec<<"; bytes="<<bytes.size()<<'\n';}
#endif
