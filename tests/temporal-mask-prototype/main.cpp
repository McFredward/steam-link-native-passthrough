#include "prototype.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace tp=temporal_prototype;
using Clock=std::chrono::steady_clock;
unsigned checks=0;
void check(bool yes,const char* why){++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
tp::Source source(unsigned w,unsigned h){tp::Source s;s.binding[0]=73;s.sourceEpoch=11;s.encoderEpoch=29;s.width=w;s.height=h;return s;}
tp::Pixels alphabet(const tp::Source& s,unsigned frame){tp::Pixels p(std::size_t(s.width)*s.height);std::uint32_t random=739;for(std::size_t i=0;i<p.size();++i){random=random*1664525u+1013904223u;p[i]=std::uint8_t((i<256?i:(random>>24))+frame);}return p;}
tp::Packet packet(tp::Sender& sender,const tp::Source& s,unsigned frame,const tp::Pixels& raw){tp::Encoded baseline;check(tp::encodeIndependent(raw,baseline),"independent baseline succeeds");return sender.encode({s,frame},raw,baseline);}
void equals(const tp::Result& result,const tp::Key& expected,const tp::Pixels& raw){check(result.status==tp::Status::Ok&&result.image&&result.image->current==expected&&result.image->pixels==raw,"decoded output is the exact current image and all R8 bytes");}
void refusal(const tp::Result& result){check(result.status!=tp::Status::Ok&&!result.image&&!result.acknowledgment,"refused input publishes no old or partial image or ACK");}
void tests(){
    const auto s=source(256,128);
    for(auto method:{tp::Method::AnchorSubtract,tp::Method::AnchorXor,tp::Method::AnchorSubtractUp,tp::Method::AnchorXorUp}){
        tp::Sender sender(s,method);tp::Receiver receiver(s);
        auto first=packet(sender,s,1,alphabet(s,0));auto a=receiver.decode(first,{s,1});equals(a,{s,1},alphabet(s,0));check(a.acknowledgment&&sender.acknowledge(*a.acknowledgment),"only exact completed keyframe ACK admits an anchor");
        auto second=packet(sender,s,2,alphabet(s,1));check(second.mode!=tp::Mode::Independent,"all256-value temporal control chooses a useful delta");auto b=receiver.decode(second,{s,2});equals(b,{s,2},alphabet(s,1));
        tp::Receiver missing(s);refusal(missing.decode(second,{s,2}));
        auto bad=second;bad.base.digest[0]^=1;tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        bad=second;bad.base.key.frame=second.current.frame;tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        bad=second;++bad.base.key.source.sourceEpoch;tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        bad=second;++bad.base.key.source.encoderEpoch;tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        bad=second;++bad.base.key.source.width;tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        bad=second;bad.data.back()^=1;refusal(receiver.decode(bad,{s,2}));
        bad=second;bad.mode=tp::Mode(255);tp::sealForFixture(bad);refusal(receiver.decode(bad,{s,2}));
        refusal(receiver.decode(first,{s,2}));
        equals(receiver.decode(second,{s,2}),{s,2},alphabet(s,1));
        check(receiver.cacheEntries()<=3&&receiver.cacheBytes()<=3*tp::MaximumPixels,"references remain logically bounded after refusals");
    }
    {
        tp::Sender sender(s,tp::Method::AnchorSubtract,6);tp::Receiver receiver(s);
        auto first=packet(sender,s,1,alphabet(s,0));auto r=receiver.decode(first,{s,1});equals(r,{s,1},alphabet(s,0));
        for(unsigned frame=2;frame<=4;++frame){auto full=packet(sender,s,frame,alphabet(s,frame));check(full.mode==tp::Mode::Independent&&!full.anchorCandidate,"pending anchor remains immutable during delayed ACK while current full pictures continue");auto picture=receiver.decode(full,{s,frame});equals(picture,{s,frame},alphabet(s,frame));check(!picture.acknowledgment,"nonanchor independent picture does not replace pending ACK identity");}
        check(sender.acknowledge(*r.acknowledgment),"delayed initial ACK remains usable");auto next=packet(sender,s,5,alphabet(s,5));check(next.base.key.frame==1,"next frame uses exactly acknowledged retained anchor");equals(receiver.decode(next,{s,5}),{s,5},alphabet(s,5));
    }
    {
        tp::Sender sender(s,tp::Method::AnchorSubtract,3);tp::Receiver receiver(s);
        auto lost=packet(sender,s,1,alphabet(s,0));(void)lost;
        for(unsigned frame=2;frame<=3;++frame){auto p=packet(sender,s,frame,alphabet(s,frame));check(!p.anchorCandidate&&p.mode==tp::Mode::Independent,"lost initial anchor does not stall or invent a base");equals(receiver.decode(p,{s,frame}),{s,frame},alphabet(s,frame));}
        auto retry=packet(sender,s,4,alphabet(s,4));check(retry.anchorCandidate,"bounded independent refresh replaces lost unacknowledged offer");auto r=receiver.decode(retry,{s,4});check(sender.acknowledge(*r.acknowledgment),"replacement has exact completed ACK");auto p=packet(sender,s,5,alphabet(s,5));equals(receiver.decode(p,{s,5}),{s,5},alphabet(s,5));
    }
    {
        tp::Sender sender(s,tp::Method::AnchorSubtract,3);tp::Receiver receiver(s);
        auto first=packet(sender,s,1,alphabet(s,0));auto r=receiver.decode(first,{s,1});check(sender.acknowledge(*r.acknowledgment),"first anchor acknowledged");
        auto second=packet(sender,s,2,alphabet(s,1));equals(receiver.decode(second,{s,2}),{s,2},alphabet(s,1));
        auto third=packet(sender,s,3,alphabet(s,2));equals(receiver.decode(third,{s,3}),{s,3},alphabet(s,2));
        auto fourth=packet(sender,s,4,alphabet(s,3));check(fourth.anchorCandidate,"periodic independent refresh offered");r=receiver.decode(fourth,{s,4});equals(r,{s,4},alphabet(s,3));
        auto fifth=packet(sender,s,5,alphabet(s,4));check(fifth.base.key.frame==1,"unacknowledged replacement never becomes compression authority");equals(receiver.decode(fifth,{s,5}),{s,5},alphabet(s,4));
        auto wrong=fourth.reference();wrong.digest[0]^=1;check(!sender.acknowledge(wrong),"wrong digest ACK rejected");wrong=fourth.reference();++wrong.key.source.encoderEpoch;check(!sender.acknowledge(wrong),"foreign epoch ACK rejected");check(!sender.acknowledge(first.reference()),"stale ACK rejected");
        check(sender.acknowledge(fourth.reference()),"matching delayed ACK switches immutable anchor");check(!sender.acknowledge(fourth.reference()),"duplicate ACK cannot renew a reference");auto sixth=packet(sender,s,6,alphabet(s,5));check(sixth.base.key.frame==4,"delta explicitly names acknowledged replacement");equals(receiver.decode(sixth,{s,6}),{s,6},alphabet(s,5));
        equals(receiver.decode(third,{s,3}),{s,3},alphabet(s,2));refusal(receiver.decode(third,{s,6}));
        for(unsigned change=0;change<3;++change){auto next=s;if(change==0)++next.sourceEpoch;else if(change==1)++next.encoderEpoch;else next.width=192;sender.reset(next);receiver.reset(next);check(!receiver.cacheEntries()&&!sender.cacheBytes(),"source/encoder/extent replacement clears references");refusal(receiver.decode(sixth,{s,6}));check(!sender.acknowledge(fourth.reference()),"old ACK cannot revive after reset");const auto raw=alphabet(next,8);auto fresh=packet(sender,next,1,raw);check(fresh.mode==tp::Mode::Independent,"replacement begins independently");r=receiver.decode(fresh,{next,1});equals(r,{next,1},raw);check(sender.acknowledge(*r.acknowledgment),"replacement has exact ACK");}
    }
    {
        tp::Sender sender(s,tp::Method::Independent);tp::Receiver receiver(s,true,true,3);
        auto first=packet(sender,s,1,alphabet(s,0));equals(receiver.decode(first,{s,1}),{s,1},alphabet(s,0));auto conflicting=packet(sender,s,1,alphabet(s,77));refusal(receiver.decode(conflicting,{s,1}));
        for(unsigned i=2;i<=30;++i){const auto raw=alphabet(s,i);auto p=packet(sender,s,i,raw);equals(receiver.decode(p,{s,i}),{s,i},raw);check(receiver.cacheEntries()<=3&&receiver.cacheBytes()<=3*std::size_t(s.width)*s.height,"keyframe bursts retain at most three bases");}
    }
    std::printf("{\"validation_checks\":%u,\"all256_alpha\":true,\"wrong_missing_base\":true,\"stale_wrong_ack\":true,\"reordering\":true,\"source_encoder_extent_reset\":true,\"bounded_cache\":true}\n",checks);
}

constexpr unsigned Width=1152,Height=4608;
class Sequence {
public:
    Sequence(){cloud_.resize(Width*Height);for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<160;++x){const double c=.5+.25*std::sin(double(x)/73+double(y)/123)+.25*std::cos(double(x)/131-double(y)/79);cloud_[std::size_t(y)*Width+x]=std::uint8_t(c*255);}
        for(unsigned y=Height/3;y<2*Height/3;++y)std::fill_n(cloud_.data()+std::size_t(y)*Width+Width/3,Width/3,std::uint8_t(255));
    }
    tp::Pixels frame(unsigned kind,unsigned frame)const{
        tp::Pixels out(cloud_.size());std::uint32_t random=739+frame*1531;
        if(kind>=5){for(auto& p:out){random^=random<<13;random^=random>>17;random^=random<<5;p=std::uint8_t(kind==5?random:247+random%9);}return out;}
        if(!kind||kind==2)out=cloud_;
        else if(kind==4&&frame>=20&&frame<40){for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)out[std::size_t(y)*Width+x]=std::uint8_t(((x/192+y/384)%2)?255:0);}
        else for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
            unsigned sx=(x+(kind==3?frame*11+(y/128)*(frame%17):frame/2))%Width;
            unsigned sy=(y+(kind==3?frame*7+(x/64)*(frame%13):frame*2))%Height;
            if(kind==4&&frame>=40){sx=(Width-1-x+frame*3)%Width;sy=(Height-1-y+frame*5)%Height;}
            auto p=cloud_[std::size_t(sy)*Width+sx];if(kind==1&&p!=255)p=std::uint8_t(unsigned(p)*(240+frame%16)/255);out[std::size_t(y)*Width+x]=p;
        }
        if(kind==2){
            for(unsigned particle=0;particle<200;++particle){const int cx=int((particle*89+frame*(1+particle%7))%Width),cy=int((particle*173+frame*(2+particle%11))%Height);
                for(int dy=-4;dy<=4;++dy)for(int dx=-4;dx<=4;++dx){const int x=cx+dx,y=cy+dy;if(x<0||y<0||x>=int(Width)||y>=int(Height)||dx*dx+dy*dy>16)continue;auto& p=out[std::size_t(y)*Width+unsigned(x)];const unsigned alpha=unsigned(17-dx*dx-dy*dy)*15;p=std::uint8_t(255-((255-p)*(255-alpha)+127)/255);}
            }
        }
        return out;
    }
private:tp::Pixels cloud_;
};
std::vector<tp::Method> methods(){return {tp::Method::Independent,tp::Method::PreviousSubtract,tp::Method::AnchorSubtract,tp::Method::AnchorXor,tp::Method::AnchorSubtractUp,tp::Method::AnchorXorUp};}
struct Stats {
    std::vector<double> encode,decode,keyEncode,keyDecode;std::vector<std::size_t> bytes,keyBytes;
    unsigned keyframes=0,deltas=0;std::size_t senderPeak=0,receiverPeak=0;
};
double mean(const std::vector<double>& v){double n=0;for(auto x:v)n+=x;return v.empty()?0:n/v.size();}
double p95(std::vector<double> v){if(v.empty())return 0;std::sort(v.begin(),v.end());return v[std::min(v.size()-1,v.size()*95/100)];}
void benchmark(){
    const Sequence sequence;const auto s=source(Width,Height);const auto configs=methods();
    for(unsigned kind=0;kind<7;++kind){const unsigned frames=kind>=5?24:60;
        std::vector<tp::Sender> senders;std::vector<tp::Receiver> receivers;std::vector<Stats> stats(configs.size());
        for(auto method:configs){senders.emplace_back(s,method,15);receivers.emplace_back(s,method==tp::Method::PreviousSubtract,method!=tp::Method::Independent,3);}
        for(unsigned n=1;n<=frames;++n){const auto raw=sequence.frame(kind,n-1);tp::Encoded independent;const auto b0=Clock::now();check(tp::encodeIndependent(raw,independent),"benchmark independent baseline");const double baseMs=std::chrono::duration<double,std::milli>(Clock::now()-b0).count();
            for(unsigned step=0;step<configs.size();++step){const auto i=(step+n)%configs.size();const tp::Key current{s,n};const auto t0=Clock::now();const auto p=senders[i].encode(current,raw,independent);const auto t1=Clock::now();const auto result=receivers[i].decode(p,current);const auto t2=Clock::now();equals(result,current,raw);
                double encMs=baseMs+std::chrono::duration<double,std::milli>(t1-t0).count();const auto decMs=std::chrono::duration<double,std::milli>(t2-t1).count();
                if(result.acknowledgment&&configs[i]!=tp::Method::Independent&&configs[i]!=tp::Method::PreviousSubtract){const auto a0=Clock::now();check(senders[i].acknowledge(*result.acknowledgment),"benchmark exact synchronous ACK");encMs+=std::chrono::duration<double,std::milli>(Clock::now()-a0).count();}
                check(p.bytes()<=independent.data.size(),"temporal candidate never exceeds current independent mask payload");auto& v=stats[i];v.encode.push_back(encMs);v.decode.push_back(decMs);v.bytes.push_back(p.bytes());
                if(p.mode==tp::Mode::Independent){++v.keyframes;v.keyBytes.push_back(p.bytes());v.keyEncode.push_back(encMs);v.keyDecode.push_back(decMs);}else ++v.deltas;
                v.senderPeak=std::max(v.senderPeak,senders[i].peakCacheBytes());v.receiverPeak=std::max(v.receiverPeak,receivers[i].peakCacheBytes());
            }
        }
        for(unsigned i=0;i<configs.size();++i){const auto& v=stats[i];std::uint64_t total=0,keyTotal=0;for(auto bytes:v.bytes)total+=bytes;for(auto bytes:v.keyBytes)keyTotal+=bytes;
            std::printf("{\"sequence\":\"%s\",\"method\":\"%s\",\"frames\":%u,\"fps_for_budget\":90,\"keyframe_interval\":15,\"mask_bytes\":%llu,\"mean_mask_bytes\":%.3f,\"keyframes\":%u,\"deltas\":%u,\"keyframe_bytes\":%llu,\"encode_mean_ms\":%.3f,\"encode_p95_ms\":%.3f,\"decode_mean_ms\":%.3f,\"decode_p95_ms\":%.3f,\"keyframe_encode_p95_ms\":%.3f,\"keyframe_decode_p95_ms\":%.3f,\"sender_cache_peak_bytes\":%zu,\"receiver_cache_peak_bytes\":%zu}\n",
                std::array<const char*,7>{{"static-cloud-control","moving-animated-clouds","moving-particles","gaze-like-remapping-stress","abrupt-scenes","full-noise-stress","near-opaque-noise-stress"}}[kind],tp::name(configs[i]),frames,static_cast<unsigned long long>(total),double(total)/frames,v.keyframes,v.deltas,static_cast<unsigned long long>(keyTotal),mean(v.encode),p95(v.encode),mean(v.decode),p95(v.decode),p95(v.keyEncode),p95(v.keyDecode),v.senderPeak,v.receiverPeak);
        }
    }
}
void lossSimulation(){
    const Sequence sequence;const auto s=source(Width,Height);
    for(auto method:{tp::Method::PreviousSubtract,tp::Method::AnchorSubtract,tp::Method::AnchorSubtractUp}){
        tp::Sender sender(s,method,15);tp::Receiver receiver(s,method==tp::Method::PreviousSubtract,true,3);
        unsigned droppedDelta=0,droppedKey=0,droppedAck=0,missing=0,decoded=0,keyframes=0;std::uint64_t bytes=0;
        for(unsigned n=1;n<=60;++n){const auto raw=sequence.frame(1,n-1);auto p=packet(sender,s,n,raw);bytes+=p.bytes();const bool key=p.mode==tp::Mode::Independent;if(key)++keyframes;
            if(!droppedDelta&&n>=8&&!key){++droppedDelta;continue;}
            if(!droppedKey&&n>=16&&key){++droppedKey;continue;}
            auto result=receiver.decode(p,{s,n});if(result.status!=tp::Status::Ok){check(!result.image,"lost reference never publishes stale image");++missing;continue;}
            equals(result,{s,n},raw);++decoded;
            if(result.acknowledgment&&method!=tp::Method::PreviousSubtract){if(!droppedAck&&n>=31){++droppedAck;continue;}check(sender.acknowledge(*result.acknowledgment),"loss simulation ACK names a completed exact keyframe");}
        }
        check(droppedDelta==1&&droppedKey==1,"simulation includes both a missing delta and independent keyframe");
        if(method!=tp::Method::PreviousSubtract)check(!missing&&droppedAck==1,"acknowledged anchors avoid cascade after delta/keyframe/ACK loss");
        else check(missing>1,"previous-frame reference chain exposes loss amplification");
        std::printf("{\"loss_simulation\":\"%s\",\"frames\":60,\"dropped_deltas\":%u,\"dropped_keyframes\":%u,\"dropped_acks\":%u,\"extra_missing_base_frames\":%u,\"decoded_frames\":%u,\"keyframes\":%u,\"sent_mask_bytes\":%llu}\n",tp::name(method),droppedDelta,droppedKey,droppedAck,missing,decoded,keyframes,static_cast<unsigned long long>(bytes));
    }
}
int main(int argc,char** argv){if(argc==5&&std::string(argv[1])=="--decode-mask"){const unsigned mode=unsigned(std::strtoul(argv[2],nullptr,10)),width=unsigned(std::strtoul(argv[3],nullptr,10)),height=unsigned(std::strtoul(argv[4],nullptr,10));if(mode<1||mode>5||!width||!height||width>8192||height>8192||std::size_t(width)*height>tp::MaximumPixels)return 2;tp::Pixels data;std::array<std::uint8_t,65536> block{};for(;;){const auto n=std::fread(block.data(),1,block.size(),stdin);if(!n)break;if(data.size()+n>tp::MaximumPixels)return 2;data.insert(data.end(),block.begin(),block.begin()+std::ptrdiff_t(n));}stream_bridge::CertifiedAccessUnit frame;frame.descriptor={1,1,std::uint16_t(width),std::uint16_t(height)};frame.coverageEncoding=stream_bridge::CoverageEncoding(mode);frame.coverage=std::move(data);tp::Pixels raw;if(!stream_bridge::decodeCertifiedCoverage(frame,raw)||raw.size()!=std::size_t(width)*height)return 1;return std::fwrite(raw.data(),1,raw.size(),stdout)==raw.size()?0:1;}if(argc==4&&std::string(argv[1])=="--baseline-r8"){const unsigned width=unsigned(std::strtoul(argv[2],nullptr,10)),height=unsigned(std::strtoul(argv[3],nullptr,10));if(!width||!height||width>8192||height>8192||std::size_t(width)*height>tp::MaximumPixels)return 2;tp::Pixels raw(std::size_t(width)*height);for(unsigned index=0;index<128;++index){const auto count=std::fread(raw.data(),1,raw.size(),stdin);if(!count)return 0;if(count!=raw.size())return 2;tp::Encoded encoded;const auto started=Clock::now();if(!tp::encodeIndependent(raw,encoded))return 1;const auto ended=Clock::now();stream_bridge::CertifiedAccessUnit frame;frame.descriptor={index+1,1,std::uint16_t(width),std::uint16_t(height)};frame.coverageEncoding=encoded.encoding;frame.coverage=encoded.data;tp::Pixels decoded;if(!stream_bridge::decodeCertifiedCoverage(frame,decoded)||decoded!=raw)return 1;const auto done=Clock::now();std::printf("{\"index\":%u,\"bytes\":%zu,\"encoding\":%u,\"encode_ms\":%.6f,\"decode_ms\":%.6f,\"bitexact\":true}\n",index,encoded.data.size(),unsigned(encoded.encoding),std::chrono::duration<double,std::milli>(ended-started).count(),std::chrono::duration<double,std::milli>(done-ended).count());}return std::fgetc(stdin)==EOF?0:2;}if(argc==5&&std::string(argv[1])=="--raw"){const unsigned kind=unsigned(std::strtoul(argv[2],nullptr,10)),frames=unsigned(std::strtoul(argv[3],nullptr,10));if(kind>6||!frames||frames>120)return 2;const Sequence sequence;const tp::Pixels neutral(Width*Height/4,128);for(unsigned n=0;n<frames;++n){const auto raw=sequence.frame(kind,n);if(std::fwrite(raw.data(),1,raw.size(),stdout)!=raw.size())return 1;if(std::string(argv[4])=="420")for(unsigned c=0;c<2;++c)if(std::fwrite(neutral.data(),1,neutral.size(),stdout)!=neutral.size())return 1;}return 0;}if(argc==2&&std::string(argv[1])=="--benchmark")benchmark();else if(argc==2&&std::string(argv[1])=="--loss")lossSimulation();else tests();std::printf("{\"checks\":%u,\"private_prototype\":true,\"hardware_test\":false}\n",checks);}
