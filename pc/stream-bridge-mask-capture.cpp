#include "stream-bridge-mask-capture.h"
#include "receiver/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace stream_bridge::sender::capture {
namespace {
void put(std::uint8_t* p,std::uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[i]=std::uint8_t(value>>(8*i));}
std::uint64_t get(const std::uint8_t* p,unsigned bytes){std::uint64_t result=0;for(unsigned i=0;i<bytes;++i)result|=std::uint64_t(p[i])<<(8*i);return result;}
bool zeros(const std::uint8_t* p,std::size_t size){for(std::size_t i=0;i<size;++i)if(p[i])return false;return true;}
std::array<std::uint8_t,32> hash(Bytes prefix,Bytes bytes)noexcept{frame_receiver::Sha256 h;if(prefix.size)h.update(prefix.data,prefix.size);if(bytes.size)h.update(bytes.data,bytes.size);return h.finish();}
bool sameSource(const Metadata& a,const Metadata& b){return a.pid==b.pid&&a.sourceContract==b.sourceContract&&a.encoderGeneration==b.encoderGeneration&&a.width==b.width&&a.height==b.height;}
bool plainPath(const std::filesystem::path& path){
    for(auto current=path;!current.empty();){
        std::error_code error;const auto status=std::filesystem::symlink_status(current,error);
        if(!error&&std::filesystem::is_symlink(status))return false;
        if(!error&&std::filesystem::is_regular_file(status)){const auto links=std::filesystem::hard_link_count(current,error);if(error||links!=1)return false;}
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
        const auto parent=current.parent_path();if(parent==current)break;current=parent;
    }
    return true;
}
constexpr const char* Owner="SFNP private mask capture v1\n";
constexpr const char* Request="SFNP_MASK_CAPTURE_V1\n";
std::string hex(const Token& token){constexpr char digits[]="0123456789abcdef";std::string out;for(auto value:token){out.push_back(digits[value>>4]);out.push_back(digits[value&15]);}return out;}
bool requestToken(const std::string& value,Token& token){
    if(value.size()!=std::strlen(Request)+33||value.compare(0,std::strlen(Request),Request)||value.back()!='\n')return false;
    Token result{};for(unsigned i=0;i<32;++i){const auto c=value[std::strlen(Request)+i];const auto at=std::string("0123456789abcdef").find(c);if(at==std::string::npos)return false;result[i/2]|=std::uint8_t(at<<((i%2)?0:4));}
    if(zeros(result.data(),result.size()))return false;
    token=result;return true;
}
std::string smallFile(const std::filesystem::path& file,std::size_t limit){
    if(!plainPath(file)||!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>limit)return {};
    std::ifstream stream(file,std::ios::binary);std::string value(limit+1,'\0');stream.read(value.data(),static_cast<std::streamsize>(value.size()));
    const auto size=static_cast<std::size_t>(stream.gcount());if(size>limit)return {};value.resize(size);return value;
}
}
bool valid(const Metadata& m,std::size_t size)noexcept{
    const auto pixels=std::size_t(m.width)*m.height;const auto encoding=unsigned(m.encoding);
    return m.frame&&m.encoderGeneration&&m.sourceContract&&m.timestampUs&&m.pid&&pixels&&pixels<=MaximumCoverageBytes&&
        size&&size<=MaximumPayload&&encoding>=1&&encoding<=5&&
        (m.encoding!=CoverageEncoding::Raw||size==pixels)&&m.originalVideoBytes&&m.completeAuBytes>=m.originalVideoBytes&&m.qosFlags<=7;
}
std::array<std::uint8_t,HeaderBytes> header(const Token& token)noexcept{
    std::array<std::uint8_t,HeaderBytes> out{};std::memcpy(out.data(),"SFNPMASK",8);put(out.data()+8,1,2);put(out.data()+10,HeaderBytes,2);
    put(out.data()+12,MaximumFrames,4);put(out.data()+16,MaximumBytes,8);std::copy(token.begin(),token.end(),out.begin()+24);return out;
}
bool parseHeader(Bytes input,Token& token)noexcept{
    if(!input.data||input.size!=HeaderBytes||std::memcmp(input.data,"SFNPMASK",8)||get(input.data+8,2)!=1||get(input.data+10,2)!=HeaderBytes||
       get(input.data+12,4)!=MaximumFrames||get(input.data+16,8)!=MaximumBytes||!zeros(input.data+40,8)||zeros(input.data+24,16))return false;
    std::copy_n(input.data+24,16,token.begin());return true;
}
std::array<std::uint8_t,RecordBytes> recordHeader(const Sample& sample){
    std::array<std::uint8_t,RecordBytes> out{};auto* p=out.data();const auto& m=sample.metadata;std::memcpy(p,"MSK1",4);put(p+4,RecordBytes,4);put(p+8,sample.payload.size(),4);put(p+12,unsigned(m.encoding),4);
    put(p+16,m.frame,8);put(p+24,m.encoderGeneration,8);put(p+32,m.sourceContract,8);put(p+40,m.timestampUs,8);put(p+48,m.pid,4);put(p+52,m.width,2);put(p+54,m.height,2);
    put(p+56,m.originalVideoBytes,4);put(p+60,m.completeAuBytes,4);put(p+64,m.encodeUs,4);put(p+68,m.qosFlags,4);put(p+72,m.qosRevision,8);
    const std::uint32_t qos[]{m.frameRateNum,m.frameRateDen,m.requestedAverageBitRate,m.requestedMaximumBitRate,m.requestedVbvBufferSize,m.requestedVbvInitialDelay,
        m.appliedAverageBitRate,m.appliedMaximumBitRate,m.appliedVbvBufferSize,m.appliedVbvInitialDelay,m.reservedMetadataBitRate,m.meanMetadataBitRate};
    for(unsigned i=0;i<12;++i)put(p+80+i*4,qos[i],4);
    put(p+128,m.nativeInputTimestamp,8);
    const auto digest=hash({p,144},{sample.payload.data(),sample.payload.size()});std::copy(digest.begin(),digest.end(),out.begin()+144);return out;
}
bool parseRecordHeader(Bytes input,Metadata& metadata,std::uint32_t& payloadBytes,std::array<std::uint8_t,32>& digest)noexcept{
    if(!input.data||input.size!=RecordBytes||std::memcmp(input.data,"MSK1",4)||get(input.data+4,4)!=RecordBytes||!zeros(input.data+136,8))return false;
    const auto* p=input.data;Metadata m{};m.encoding=CoverageEncoding(get(p+12,4));m.frame=get(p+16,8);m.encoderGeneration=get(p+24,8);m.sourceContract=get(p+32,8);m.timestampUs=get(p+40,8);m.pid=std::uint32_t(get(p+48,4));m.width=std::uint16_t(get(p+52,2));m.height=std::uint16_t(get(p+54,2));m.originalVideoBytes=std::uint32_t(get(p+56,4));m.completeAuBytes=std::uint32_t(get(p+60,4));m.encodeUs=std::uint32_t(get(p+64,4));const auto size=std::uint32_t(get(p+8,4));
    m.qosFlags=std::uint32_t(get(p+68,4));m.qosRevision=get(p+72,8);
    std::uint32_t* const qos[]{&m.frameRateNum,&m.frameRateDen,&m.requestedAverageBitRate,&m.requestedMaximumBitRate,&m.requestedVbvBufferSize,&m.requestedVbvInitialDelay,
        &m.appliedAverageBitRate,&m.appliedMaximumBitRate,&m.appliedVbvBufferSize,&m.appliedVbvInitialDelay,&m.reservedMetadataBitRate,&m.meanMetadataBitRate};
    for(unsigned i=0;i<12;++i)*qos[i]=std::uint32_t(get(p+80+i*4,4));
    m.nativeInputTimestamp=get(p+128,8);
    if(get(p+12,4)>5||!valid(m,size))return false;
    metadata=m;payloadBytes=size;std::copy_n(p+144,32,digest.begin());return true;
}
bool verifyRecord(Bytes record,Bytes bytes)noexcept{
    if(!record.data||record.size!=RecordBytes||!bytes.data||!bytes.size||bytes.size>MaximumPayload)return false;
    const auto digest=hash({record.data,144},bytes);return std::equal(digest.begin(),digest.end(),record.data+144);
}
std::array<std::uint8_t,FooterBytes> footer(Stop reason,std::uint32_t frames,std::uint64_t bytes,std::uint64_t lastFrame)noexcept{
    std::array<std::uint8_t,FooterBytes> out{};std::memcpy(out.data(),"SFNPDONE",8);put(out.data()+8,unsigned(reason),4);put(out.data()+12,frames,4);put(out.data()+16,bytes,8);put(out.data()+24,lastFrame,8);return out;
}
bool parseFooter(Bytes input,Stop& reason,std::uint32_t& frames,std::uint64_t& bytes,std::uint64_t& lastFrame)noexcept{
    if(!input.data||input.size!=FooterBytes||std::memcmp(input.data,"SFNPDONE",8)||!get(input.data+8,4)||get(input.data+8,4)>unsigned(Stop::IoError)||get(input.data+12,4)>MaximumFrames||get(input.data+16,8)>MaximumBytes||get(input.data+16,8)<HeaderBytes+FooterBytes||
       (get(input.data+8,4)==unsigned(Stop::Complete)&&get(input.data+12,4)!=MaximumFrames)||
       (get(input.data+12,4)==0)!=(get(input.data+24,8)==0))return false;
    reason=Stop(get(input.data+8,4));frames=std::uint32_t(get(input.data+12,4));bytes=get(input.data+16,8);lastFrame=get(input.data+24,8);return true;
}
const char* stopName(Stop s)noexcept{switch(s){case Stop::Capturing:return "capturing";case Stop::Complete:return "complete";case Stop::ByteLimit:return "byte-limit";case Stop::QueueBusy:return "queue-busy";case Stop::QueueFull:return "queue-full";case Stop::SourceChanged:return "source-changed";case Stop::FrameGap:return "frame-gap";case Stop::Timeout:return "timeout";case Stop::Stopped:return "stopped";case Stop::InvalidSample:return "invalid-sample";case Stop::IoError:return "io-error";}return "invalid";}
void Queue::start(){std::lock_guard<std::mutex> lock(mutex_);for(auto& s:samples_)s={};read_=count_=frames_=0;bytes_=HeaderBytes+FooterBytes;last_={};epoch_.fetch_add(1,std::memory_order_acq_rel);reason_.store(Stop::Capturing,std::memory_order_release);}
void Queue::stop(Stop reason)noexcept{auto expected=Stop::Capturing;reason_.compare_exchange_strong(expected,reason,std::memory_order_acq_rel);changed_.notify_one();}
void Queue::offer(const Metadata& metadata,std::vector<std::uint8_t>&& payload)noexcept{
    const auto epoch=epoch_.load(std::memory_order_acquire);if(!active())return;
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock){stop(Stop::QueueBusy);return;}
    if(!active()||epoch_.load(std::memory_order_acquire)!=epoch)return;
    if(!valid(metadata,payload.size())){stop(Stop::InvalidSample);return;}
    if(frames_&&!sameSource(last_,metadata)){stop(Stop::SourceChanged);return;}
    if(frames_&&(last_.frame==UINT64_MAX||metadata.frame!=last_.frame+1||metadata.timestampUs<last_.timestampUs)){stop(Stop::FrameGap);return;}
    if(count_==samples_.size()){stop(Stop::QueueFull);return;}
    if(payload.size()+RecordBytes>MaximumBytes-bytes_){stop(Stop::ByteLimit);return;}
    auto& slot=samples_[(read_+count_)%samples_.size()];slot.metadata=metadata;slot.payload=std::move(payload);++count_;++frames_;bytes_+=RecordBytes+slot.payload.size();last_=metadata;
    if(frames_==MaximumFrames)stop(Stop::Complete);
    changed_.notify_one();
}
bool Queue::pop(Sample& sample){std::lock_guard<std::mutex> lock(mutex_);if(!count_)return false;sample=std::move(samples_[read_]);read_=(read_+1)%samples_.size();--count_;return true;}
void Queue::discard(){std::lock_guard<std::mutex> lock(mutex_);for(auto& sample:samples_)sample={};count_=read_=0;}
void Queue::wait(){std::unique_lock<std::mutex> lock(mutex_);changed_.wait_for(lock,std::chrono::milliseconds(50),[this]{return count_||!active();});}
struct Writer::Impl {
    std::filesystem::path root;Queue queue;std::thread worker;std::atomic<bool> finished{true};
    Token lastToken{};
    std::chrono::steady_clock::time_point next{};
    void status(const Token& token,const char* state,std::uint32_t count,std::uint64_t bytes){
        const auto target=root/"mask-capture/status.json";if(!plainPath(target))throw std::runtime_error("linked status");
        std::ofstream stream(target,std::ios::binary|std::ios::trunc);stream<<"{\"schema\":\"sfnp-mask-capture-status-v1\",\"token\":\""<<hex(token)<<"\",\"state\":\""<<state<<"\",\"frames\":"<<count<<",\"bytes\":"<<bytes<<"}\n";
        stream.close();if(!stream)throw std::runtime_error("capture status write failed");
    }
    bool ownDirectory(){
        const auto folder=root/"mask-capture";if(!plainPath(folder))return false;
        if(std::filesystem::exists(folder)){
            if(!std::filesystem::is_directory(folder)||smallFile(folder/"owner.txt",64)!=Owner)return false;
        }else{std::filesystem::create_directory(folder);std::ofstream stream(folder/"owner.txt",std::ios::binary);stream<<Owner;stream.close();if(!stream)return false;}
        const auto target=folder/"latest.sfmc";if(!plainPath(target))return false;
        if(std::filesystem::exists(target)){
            if(!std::filesystem::is_regular_file(target))return false;
            std::ifstream stream(target,std::ios::binary);std::array<char,8> magic{};stream.read(magic.data(),magic.size());if(!stream||std::memcmp(magic.data(),"SFNPMASK",8))return false;
        }
        const auto statusPath=folder/"status.json";if(!plainPath(statusPath))return false;
        if(std::filesystem::exists(statusPath)&&smallFile(statusPath,1024).find("\"schema\":\"sfnp-mask-capture-status-v1\"")==std::string::npos)return false;
        return true;
    }
    void run(Token token)noexcept{
        std::uint32_t count=0;std::uint64_t bytes=HeaderBytes,lastFrame=0;Stop reason=Stop::IoError;
        try{
            const auto target=root/"mask-capture/latest.sfmc";if(!plainPath(target))throw std::runtime_error("linked capture");
            std::ofstream stream(target,std::ios::binary|std::ios::trunc);stream.exceptions(std::ios::badbit|std::ios::failbit);
            const auto prefix=header(token);stream.write(reinterpret_cast<const char*>(prefix.data()),prefix.size());
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);Sample sample;
            for(;;){
                if(queue.pop(sample)){
                    const auto record=recordHeader(sample);stream.write(reinterpret_cast<const char*>(record.data()),record.size());stream.write(reinterpret_cast<const char*>(sample.payload.data()),sample.payload.size());
                    bytes+=record.size()+sample.payload.size();++count;lastFrame=sample.metadata.frame;sample={};
                }else if(!queue.active())break;
                else{if(std::chrono::steady_clock::now()>=deadline)queue.stop(Stop::Timeout);queue.wait();}
            }
            reason=queue.reason();const auto end=footer(reason,count,bytes+FooterBytes,lastFrame);stream.write(reinterpret_cast<const char*>(end.data()),end.size());stream.close();bytes+=FooterBytes;
        }catch(...){queue.stop(Stop::IoError);reason=Stop::IoError;}
        queue.discard();try{status(token,stopName(reason),count,bytes);}catch(...){}
        finished.store(true,std::memory_order_release);
    }
};
Writer::Writer():impl_(new Impl){}
Writer::~Writer(){stop();}
void Writer::configure(const std::filesystem::path& root)noexcept{try{if(!impl_->worker.joinable()&&!impl_->queue.active())impl_->root=root;}catch(...){} }
void Writer::offer(const Metadata& metadata,std::vector<std::uint8_t>&& payload)noexcept{impl_->queue.offer(metadata,std::move(payload));}
bool Writer::active()const noexcept{return impl_->queue.active();}
void Writer::poll()noexcept{
    auto& s=*impl_;const auto now=std::chrono::steady_clock::now();if(s.root.empty()||now<s.next)return;s.next=now+std::chrono::seconds(1);
    try{
        const auto request=s.root/"mask-capture.request";Token token{};
        const bool requested=plainPath(request)&&std::filesystem::exists(request)&&requestToken(smallFile(request,128),token);
        if(!s.finished.load(std::memory_order_acquire)){
            if(!requested||token!=s.lastToken)s.queue.stop(Stop::Stopped);
            return;
        }
        if(s.worker.joinable())s.worker.join();
        if(!requested)return;
        if(token==s.lastToken){std::filesystem::remove(request);return;}
        if(!s.ownDirectory())return;
        s.status(token,"starting",0,0);
        s.lastToken=token;s.queue.start();s.finished.store(false,std::memory_order_release);
        try{s.worker=std::thread([&s,token]{s.run(token);});}
        catch(...){s.queue.stop(Stop::IoError);s.finished.store(true,std::memory_order_release);s.status(token,"io-error",0,0);return;}
        // The nonce is one-shot even if deletion fails. Keeping its request
        // present during capture lets Ctrl+C/delete cancel the diagnostic only.
    }catch(...){}
}
void Writer::stop()noexcept{
    auto& s=*impl_;s.queue.stop(Stop::Stopped);if(s.worker.joinable())s.worker.join();
    if(s.root.empty()||zeros(s.lastToken.data(),s.lastToken.size()))return;
    try{Token token{};const auto request=s.root/"mask-capture.request";if(requestToken(smallFile(request,128),token)&&token==s.lastToken)std::filesystem::remove(request);}catch(...){}
}
} // namespace stream_bridge::sender::capture
