#include "stream_bridge_mask_hevc.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#ifdef SFNP_MASK_HEVC_ENABLED
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}
#include <dlfcn.h>
#endif

namespace stream_bridge_mask_hevc {
namespace sb=stream_bridge;
const char* statusName(Status s)noexcept{
    constexpr const char* names[]{"ok","unavailable","invalid-identity","invalid-envelope","decoder-refused","invalid-output","wrong-frame","extra-output","allocation-refused"};
    const auto i=static_cast<unsigned>(s);return i<sizeof(names)/sizeof(*names)?names[i]:"unknown";
}
#ifdef SFNP_MASK_HEVC_ENABLED
namespace {
static_assert(LIBAVCODEC_VERSION_MAJOR==62&&LIBAVUTIL_VERSION_MAJOR==60,"Use the reviewed public FFmpeg8 ABI headers");
// Includes every identity field used by the caller. Bytes, not a private
// pointer/AVFrame layout guess, travel through the public COPY_OPAQUE contract.
using Key=std::array<std::uint8_t,66>;
Key key(const Input& input){
    Key out{};std::copy(input.source.connection.begin(),input.source.connection.end(),out.begin());
    auto put=[&](unsigned offset,std::uint64_t value,unsigned size){for(unsigned i=0;i<size;++i)out[offset+i]=std::uint8_t(value>>(i*8));};
    put(16,input.source.generation,8);put(24,input.descriptor.encoderGeneration,8);put(32,input.descriptor.sourceFrame,8);
    put(40,input.descriptor.codedWidth,2);put(42,input.descriptor.codedHeight,2);out[44]=input.descriptor.cameraRecovery;
    std::copy(input.binding.begin(),input.binding.end(),out.begin()+45);out[61]='S';out[62]='F';out[63]='M';out[64]='K';out[65]=1;return out;
}
bool nonzero(const sb::Nonce& n){return std::any_of(n.begin(),n.end(),[](auto b){return b!=0;});}
AVPixelFormat softwareFormat(AVCodecContext*,const AVPixelFormat* formats){
    for(auto* p=formats;*p!=AV_PIX_FMT_NONE;++p)if(*p==AV_PIX_FMT_YUV420P||*p==AV_PIX_FMT_YUVJ420P||*p==AV_PIX_FMT_NV12)return *p;
    return AV_PIX_FMT_NONE;
}
void installedAnchor(){}
std::filesystem::path adjacent(){Dl_info info{};if(!dladdr(reinterpret_cast<void*>(&installedAnchor),&info)||!info.dli_fname)return {};const std::filesystem::path path(info.dli_fname);return path.is_absolute()?path.parent_path():std::filesystem::path{};}
}
#endif
struct Decoder::Impl {
#ifdef SFNP_MASK_HEVC_ENABLED
    void* codecLibrary=nullptr;void* utilityLibrary=nullptr;const AVCodec* codec=nullptr;
#define SFNP_SYMBOLS(X) \
    X(avcodec_version) X(avcodec_find_decoder_by_name) X(avcodec_alloc_context3) X(avcodec_open2) X(avcodec_free_context) \
    X(avcodec_send_packet) X(avcodec_receive_frame) X(av_packet_alloc) X(av_packet_free) X(av_new_packet) \
    X(avutil_version) X(av_log_set_level) X(av_frame_alloc) X(av_frame_free) X(av_frame_unref) X(av_frame_get_plane_buffer) X(av_buffer_alloc)
#define FIELD(name) decltype(&::name) name=nullptr;
    SFNP_SYMBOLS(FIELD)
#undef FIELD
    void close(){codec=nullptr;if(codecLibrary)dlclose(codecLibrary);if(utilityLibrary)dlclose(utilityLibrary);codecLibrary=utilityLibrary=nullptr;}
    template<class T> bool symbol(void* handle,T& target,const char* name){static_assert(sizeof(T)==sizeof(void*),"public POSIX function address");void* address=dlsym(handle,name);std::memcpy(&target,&address,sizeof target);return target!=nullptr;}
    bool prepare(const Libraries& paths){
        if(codec)return true;
        if(paths.codec.empty()||paths.utility.empty()||!paths.codec.is_absolute()||!paths.utility.is_absolute())return false;
        constexpr int flags=RTLD_NOW|RTLD_LOCAL
#ifdef RTLD_DEEPBIND
            |RTLD_DEEPBIND
#endif
            ;
        utilityLibrary=dlopen(paths.utility.c_str(),flags);if(!utilityLibrary)return false;
        codecLibrary=dlopen(paths.codec.c_str(),flags);if(!codecLibrary){close();return false;}
#define LOAD_CODEC(name) if(!symbol(codecLibrary,name,#name)){close();return false;}
        LOAD_CODEC(avcodec_version) LOAD_CODEC(avcodec_find_decoder_by_name) LOAD_CODEC(avcodec_alloc_context3) LOAD_CODEC(avcodec_open2) LOAD_CODEC(avcodec_free_context)
        LOAD_CODEC(avcodec_send_packet) LOAD_CODEC(avcodec_receive_frame) LOAD_CODEC(av_packet_alloc) LOAD_CODEC(av_packet_free) LOAD_CODEC(av_new_packet)
#undef LOAD_CODEC
#define LOAD_UTILITY(name) if(!symbol(utilityLibrary,name,#name)){close();return false;}
        LOAD_UTILITY(avutil_version) LOAD_UTILITY(av_log_set_level) LOAD_UTILITY(av_frame_alloc) LOAD_UTILITY(av_frame_free) LOAD_UTILITY(av_frame_unref) LOAD_UTILITY(av_frame_get_plane_buffer) LOAD_UTILITY(av_buffer_alloc)
#undef LOAD_UTILITY
        if((avcodec_version()>>16)!=LIBAVCODEC_VERSION_MAJOR||(avutil_version()>>16)!=LIBAVUTIL_VERSION_MAJOR){close();return false;}
        // This utility instance is an owned private dependency in production.
        // Surface bounded status counters instead of per-frame codec stderr.
        av_log_set_level(AV_LOG_QUIET);codec=avcodec_find_decoder_by_name("hevc");if(!codec){close();return false;}return true;
    }
    struct Work {
        Impl& api;AVCodecContext* context=nullptr;AVPacket* packet=nullptr;AVFrame* frame=nullptr;
        ~Work(){if(frame)api.av_frame_free(&frame);if(packet)api.av_packet_free(&packet);if(context)api.avcodec_free_context(&context);}
    };
    Status decode(const Input& input,const sb::HevcCoverageView& view,DecodedMask& output){
        Work w{*this};w.context=avcodec_alloc_context3(codec);w.packet=av_packet_alloc();w.frame=av_frame_alloc();
        if(!w.context||!w.packet||!w.frame)return Status::AllocationRefused;
        w.context->thread_count=2;w.context->thread_type=FF_THREAD_SLICE;
        w.context->flags|=AV_CODEC_FLAG_COPY_OPAQUE|AV_CODEC_FLAG_LOW_DELAY;
        w.context->flags&=~AV_CODEC_FLAG_OUTPUT_CORRUPT;
        w.context->err_recognition=AV_EF_CRCCHECK|AV_EF_BITSTREAM|AV_EF_BUFFER|AV_EF_EXPLODE|AV_EF_CAREFUL;
        w.context->error_concealment=0;w.context->strict_std_compliance=FF_COMPLIANCE_STRICT;
        w.context->max_pixels=sb::MaximumCoverageBytes;w.context->apply_cropping=0;w.context->get_format=softwareFormat;
        if(avcodec_open2(w.context,codec,nullptr)<0||av_new_packet(w.packet,static_cast<int>(view.annexB.size))<0)return Status::DecoderRefused;
        std::memcpy(w.packet->data,view.annexB.data,view.annexB.size);const auto identity=key(input);
        w.packet->opaque_ref=av_buffer_alloc(identity.size());if(!w.packet->opaque_ref)return Status::AllocationRefused;
        std::memcpy(w.packet->opaque_ref->data,identity.data(),identity.size());w.packet->pts=1;w.packet->dts=1;
        if(avcodec_send_packet(w.context,w.packet)<0)return Status::DecoderRefused;
        int received=avcodec_receive_frame(w.context,w.frame);
        if(received==AVERROR(EAGAIN)){
            if(avcodec_send_packet(w.context,nullptr)<0)return Status::DecoderRefused;
            received=avcodec_receive_frame(w.context,w.frame);
        }
        if(received<0)return Status::DecoderRefused;
        const auto& f=*w.frame;
        if(!f.opaque_ref||f.opaque_ref->size!=identity.size()||std::memcmp(f.opaque_ref->data,identity.data(),identity.size())||f.pts!=1)return Status::WrongFrame;
        if(f.width!=view.width||f.height!=view.height||f.crop_top||f.crop_bottom||f.crop_left||f.crop_right||
            (f.format!=AV_PIX_FMT_YUV420P&&f.format!=AV_PIX_FMT_YUVJ420P&&f.format!=AV_PIX_FMT_NV12)||f.color_range!=AVCOL_RANGE_JPEG||
            f.decode_error_flags||(f.flags&AV_FRAME_FLAG_CORRUPT)||!(f.flags&AV_FRAME_FLAG_KEY)||f.pict_type!=AV_PICTURE_TYPE_I||!f.data[0]||f.linesize[0]<f.width)return Status::InvalidOutput;
        const auto* buffer=av_frame_get_plane_buffer(w.frame,0);if(!buffer||!buffer->data)return Status::InvalidOutput;
        const auto begin=reinterpret_cast<std::uintptr_t>(buffer->data),plane=reinterpret_cast<std::uintptr_t>(f.data[0]);
        const auto used=std::uint64_t(f.linesize[0])*(view.height-1u)+view.width;
        if(plane<begin||plane-begin>buffer->size||used>buffer->size-(plane-begin))return Status::InvalidOutput;
        DecodedMask candidate;candidate.source=input.source;candidate.descriptor=input.descriptor;candidate.binding=input.binding;
        candidate.samples.resize(std::size_t(view.width)*view.height);
        for(unsigned y=0;y<view.height;++y){const auto* row=f.data[0]+std::size_t(y)*f.linesize[0];auto* target=candidate.samples.data()+std::size_t(y)*view.width;
            std::copy_n(row,view.width,target);const auto bounds=std::minmax_element(row,row+view.width);candidate.range.minimum=std::min(candidate.range.minimum,*bounds.first);candidate.range.maximum=std::max(candidate.range.maximum,*bounds.second);}
        av_frame_unref(w.frame);received=avcodec_receive_frame(w.context,w.frame);
        if(received>=0)return Status::ExtraOutput;
        if(received!=AVERROR(EAGAIN)&&received!=AVERROR_EOF)return Status::DecoderRefused;
        if(received==AVERROR(EAGAIN)){
            const auto sent=avcodec_send_packet(w.context,nullptr);if(sent<0&&sent!=AVERROR_EOF)return Status::DecoderRefused;
            received=avcodec_receive_frame(w.context,w.frame);if(received>=0)return Status::ExtraOutput;if(received!=AVERROR_EOF)return Status::DecoderRefused;
        }
        output=std::move(candidate);return Status::Ok;
    }
#endif
};
Decoder::Decoder():impl_(new Impl){}
Decoder::~Decoder(){stopCold();}
bool Decoder::prepareCold(const Libraries& paths)noexcept{
#ifdef SFNP_MASK_HEVC_ENABLED
    try{return impl_->prepare(paths);}catch(...){impl_->close();return false;}
#else
    (void)paths;return false;
#endif
}
bool Decoder::prepareInstalledCold()noexcept{
#ifdef SFNP_MASK_HEVC_ENABLED
    try{const auto folder=adjacent();if(folder.empty())return false;return prepareCold({folder/"libavcodec-sfnp-mask.so.62",folder/"libavutil-sfnp-mask.so.60"});}catch(...){return false;}
#else
    return false;
#endif
}
bool Decoder::ready()const noexcept{
#ifdef SFNP_MASK_HEVC_ENABLED
    return impl_->codec!=nullptr;
#else
    return false;
#endif
}
void Decoder::stopCold()noexcept{
#ifdef SFNP_MASK_HEVC_ENABLED
    impl_->close();
#endif
}
Status Decoder::decodeIntra(const Input& input,DecodedMask& output)noexcept{
#ifdef SFNP_MASK_HEVC_ENABLED
    if(!ready())return Status::Unavailable;
    const auto& d=input.descriptor;
    if(!nonzero(input.source.connection)||!input.source.generation||!nonzero(input.binding)||!d.sourceFrame||!d.encoderGeneration||d.cameraRecovery>2)return Status::InvalidIdentity;
    sb::HevcCoverageView view{};if(!sb::inspectHevcCoverage(input.payload,d.codedWidth,d.codedHeight,view))return Status::InvalidEnvelope;
    try{return impl_->decode(input,view,output);}catch(...){return Status::AllocationRefused;}
#else
    (void)input;(void)output;return Status::Unavailable;
#endif
}
} // namespace stream_bridge_mask_hevc
