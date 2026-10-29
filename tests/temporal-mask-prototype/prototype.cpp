#include "prototype.h"
#include "bridge/stream_bridge_compression.h"
#include "receiver/sha256.h"
#include <algorithm>
#include <stdexcept>

namespace temporal_prototype {
namespace sb=stream_bridge;
namespace {
sb::Bytes bytes(const Pixels& p){return {p.data(),p.size()};}
std::size_t count(const Source& s){return std::size_t(s.width)*s.height;}
bool valid(const Source& s){return s.sourceEpoch&&s.encoderEpoch&&s.width&&s.height&&s.width<=8192&&s.height<=8192&&count(s)<=MaximumPixels&&std::any_of(s.binding.begin(),s.binding.end(),[](auto v){return v!=0;});}
void integer(frame_receiver::Sha256& h,std::uint64_t value,unsigned n){std::uint8_t b[8]{};for(unsigned i=0;i<n;++i)b[i]=std::uint8_t(value>>(8*i));h.update(b,n);}
void key(frame_receiver::Sha256& h,const Key& k){h.update(k.source.binding.data(),k.source.binding.size());integer(h,k.source.sourceEpoch,8);integer(h,k.source.encoderEpoch,8);integer(h,k.source.width,4);integer(h,k.source.height,4);integer(h,k.frame,8);}
void addRow(std::uint8_t* __restrict target,const std::uint8_t* __restrict previous,std::size_t width){for(std::size_t x=0;x<width;++x)target[x]=std::uint8_t(target[x]+previous[x]);}
}
bool operator==(const Source& a,const Source& b){return a.binding==b.binding&&a.sourceEpoch==b.sourceEpoch&&a.encoderEpoch==b.encoderEpoch&&a.width==b.width&&a.height==b.height;}
bool operator==(const Key& a,const Key& b){return a.source==b.source&&a.frame==b.frame;}
bool operator==(const Reference& a,const Reference& b){return a.key==b.key&&a.digest==b.digest;}
Digest fingerprint(const Packet& p){frame_receiver::Sha256 hash;key(hash,p.current);integer(hash,unsigned(p.mode),1);integer(hash,unsigned(p.encoding),1);integer(hash,p.anchorCandidate,1);key(hash,p.base.key);hash.update(p.base.digest.data(),p.base.digest.size());integer(hash,p.data.size(),8);if(!p.data.empty())hash.update(p.data.data(),p.data.size());return hash.finish();}
void sealForFixture(Packet& p){p.digest=fingerprint(p);}
bool encodeIndependent(const Pixels& p,Encoded& output){return sb::encodeBestCoverage(bytes(p),true,output.encoding,output.data,true,true);}
Sender::Sender(Source s,Method m,unsigned interval):source_(s),method_(m),interval_(interval){if(!valid(s)||!interval)throw std::invalid_argument("invalid research source");}
void Sender::reset(Source s){if(!valid(s))throw std::invalid_argument("invalid source reset");source_=s;active_.reset();pending_.reset();lastKey_=0;}
std::size_t Sender::cacheBytes()const{return (active_?active_->pixels.size():0)+(pending_?pending_->pixels.size():0);}
Packet Sender::encode(const Key& current,const Pixels& pixels,const Encoded& independent){
    if(!(current.source==source_)||!current.frame||pixels.size()!=count(source_)||independent.data.empty())throw std::invalid_argument("invalid current frame");
    Packet out;out.current=current;out.encoding=independent.encoding;out.data=independent.data;
    const bool chain=method_==Method::PreviousSubtract;
    const bool mayOffer=!pending_||current.frame-pending_->reference.key.frame>=interval_;
    const bool keyframe=method_==Method::Independent||!active_||(current.frame-lastKey_>=interval_&&(chain||mayOffer));
    if(!keyframe&&out.data.size()>DeltaEnvelopeBytes+5){
        if(!(active_->reference.key.source==source_)||active_->reference.key.frame>=current.frame)throw std::logic_error("invalid active anchor");
        Pixels delta(pixels.size());const auto& base=active_->pixels;
        const bool xored=method_==Method::AnchorXor||method_==Method::AnchorXorUp;
        for(std::size_t i=0;i<pixels.size();++i)delta[i]=xored?std::uint8_t(pixels[i]^base[i]):std::uint8_t(pixels[i]-base[i]);
        const bool up=method_==Method::AnchorSubtractUp||method_==Method::AnchorXorUp;
        if(up)for(std::size_t i=delta.size();i-->source_.width;)delta[i]=std::uint8_t(delta[i]-delta[i-source_.width]);
        Pixels compressed;
        if(sb::compressCoverageRawZstd(bytes(delta),out.data.size()-DeltaEnvelopeBytes-1,compressed)&&compressed.size()+DeltaEnvelopeBytes<out.data.size()){
            out.mode=xored?(up?Mode::XorUp:Mode::Xor):(up?Mode::SubtractUp:Mode::Subtract);
            out.base=active_->reference;out.encoding=sb::CoverageEncoding::RawZstd;out.data.swap(compressed);
        }
    }
    out.anchorCandidate=method_!=Method::Independent&&out.mode==Mode::Independent&&(chain||mayOffer);
    sealForFixture(out);
    if(method_!=Method::Independent){
        if(out.anchorCandidate)lastKey_=current.frame;
        if(chain){active_=Anchor{out.reference(),pixels,0};pending_.reset();}
        else if(out.anchorCandidate)pending_=Anchor{out.reference(),pixels,0};
        peak_=std::max(peak_,cacheBytes());
    }
    return out;
}
bool Sender::acknowledge(const Reference& r){if(method_==Method::PreviousSubtract||!pending_||!(pending_->reference==r)||!(r.key.source==source_)){++rejectedAcks;return false;}active_=std::move(pending_);pending_.reset();++acceptedAcks;peak_=std::max(peak_,cacheBytes());return true;}
Receiver::Receiver(Source s,bool all,bool enabled,unsigned capacity):source_(s),cacheAll_(all),enabled_(enabled),capacity_(capacity){if(!valid(s)||!capacity||capacity>cache_.size())throw std::invalid_argument("invalid research receiver");}
void Receiver::reset(Source s){if(!valid(s))throw std::invalid_argument("invalid receiver reset");source_=s;for(auto& item:cache_)item.reset();use_=0;}
std::size_t Receiver::cacheBytes()const{std::size_t n=0;for(const auto& a:cache_)if(a)n+=a->pixels.size();return n;}
unsigned Receiver::cacheEntries()const{unsigned n=0;for(const auto& a:cache_)if(a)++n;return n;}
const Anchor* Receiver::find(const Reference& r)const{for(unsigned i=0;i<capacity_;++i)if(cache_[i]&&cache_[i]->reference==r)return &*cache_[i];return nullptr;}
void Receiver::touch(const Reference& r){for(unsigned i=0;i<capacity_;++i)if(cache_[i]&&cache_[i]->reference==r){cache_[i]->used=++use_;return;}}
bool Receiver::remember(const Reference& r,const Pixels& pixels){
    if(!enabled_)return true;
    unsigned oldest=0;
    for(unsigned i=0;i<capacity_;++i){
        if(cache_[i]&&cache_[i]->reference.key==r.key){if(!(cache_[i]->reference==r))return false;cache_[i]->used=++use_;return true;}
        if(!cache_[i]){oldest=i;break;}
        if(cache_[i]->used<cache_[oldest]->used)oldest=i;
    }
    cache_[oldest]=Anchor{r,pixels,++use_};peak_=std::max(peak_,cacheBytes());return true;
}
Result Receiver::decode(const Packet& packet,const Key& expected){
    const auto fail=[](Status s){return Result{s,std::nullopt,std::nullopt};};
    if(!(packet.current==expected)||!expected.frame)return fail(Status::WrongCurrent);
    if(!(expected.source==source_))return fail(Status::WrongSource);
    if(packet.data.empty()||packet.data.size()>sb::MaximumCoverageBytes)return fail(Status::Malformed);
    if(packet.digest!=fingerprint(packet))return fail(Status::Corrupt);
    if(packet.mode!=Mode::Independent&&packet.anchorCandidate)return fail(Status::Malformed);
    Pixels pixels;const auto total=count(source_);
    if(packet.mode==Mode::Independent){
        if(!(packet.base==Reference{}))return fail(Status::Malformed);
        sb::CertifiedAccessUnit frame;frame.descriptor={expected.frame,source_.encoderEpoch,std::uint16_t(source_.width),std::uint16_t(source_.height)};frame.coverageEncoding=packet.encoding;frame.coverage=packet.data;
        if(!sb::decodeCertifiedCoverage(frame,pixels)||pixels.size()!=total)return fail(Status::DecodeFailure);
    }else{
        if(packet.mode!=Mode::Subtract&&packet.mode!=Mode::Xor&&packet.mode!=Mode::SubtractUp&&packet.mode!=Mode::XorUp)return fail(Status::Malformed);
        if(packet.encoding!=sb::CoverageEncoding::RawZstd||!(packet.base.key.source==source_)||!packet.base.key.frame||packet.base.key.frame>=expected.frame)return fail(Status::Malformed);
        const auto* anchor=find(packet.base);if(!anchor)return fail(Status::MissingBase);
        if(!sb::decompressCoverageRawZstd(bytes(packet.data),total,pixels))return fail(Status::DecodeFailure);
        if(packet.mode==Mode::SubtractUp||packet.mode==Mode::XorUp)for(unsigned y=1;y<source_.height;++y)addRow(pixels.data()+std::size_t(y)*source_.width,pixels.data()+std::size_t(y-1)*source_.width,source_.width);
        const auto& base=anchor->pixels;
        if(packet.mode==Mode::Xor||packet.mode==Mode::XorUp)for(std::size_t i=0;i<total;++i)pixels[i]^=base[i];
        else for(std::size_t i=0;i<total;++i)pixels[i]=std::uint8_t(pixels[i]+base[i]);
    }
    if((packet.anchorCandidate||cacheAll_)&&!remember(packet.reference(),pixels))return fail(Status::BaseConflict);
    if(packet.mode!=Mode::Independent)touch(packet.base);
    const auto ack=packet.anchorCandidate?std::optional<Reference>(packet.reference()):std::nullopt;
    return {Status::Ok,Decoded{expected,std::move(pixels)},ack};
}
const char* name(Method m){switch(m){case Method::Independent:return "independent";case Method::PreviousSubtract:return "previous-subtract";case Method::AnchorSubtract:return "ack-anchor-subtract";case Method::AnchorXor:return "ack-anchor-xor";case Method::AnchorSubtractUp:return "ack-anchor-subtract-up";default:return "ack-anchor-xor-up";}}
} // namespace temporal_prototype
