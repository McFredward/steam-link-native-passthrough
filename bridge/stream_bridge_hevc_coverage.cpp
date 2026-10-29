#include "stream_bridge_hevc_coverage.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace stream_bridge {
namespace {
struct Invalid {};
void need(bool value){if(!value)throw Invalid{};}
std::uint32_t get(const std::uint8_t* p,unsigned bytes){std::uint32_t v=0;for(unsigned i=0;i<bytes;++i)v|=std::uint32_t(p[i])<<(8*i);return v;}
void put(std::uint8_t* p,std::uint32_t v,unsigned bytes){for(unsigned i=0;i<bytes;++i)p[i]=std::uint8_t(v>>(8*i));}
bool extent(unsigned w,unsigned h){return w>=8&&h>=8&&w<=8192&&h<=8192&&!(w&1)&&!(h&1);}

// The owned SEI parser validates NAL/RBSP framing but has no parameter-set or
// slice parser. This additional bounded reader checks the syntax that actually
// determines profile, memory, extent and independence before calling a decoder.
class Bits {
    std::vector<std::uint8_t> bytes_;std::size_t position_=0;
public:
    explicit Bits(Bytes ebsp){
        need(ebsp.data&&ebsp.size&&ebsp.size<=MaximumHevcCoverageBitstreamBytes);
        bytes_.reserve(ebsp.size);unsigned zeros=0;
        for(std::size_t i=0;i<ebsp.size;++i){const auto b=ebsp.data[i];
            if(zeros==2){if(b==3){need(i+1<ebsp.size&&ebsp.data[i+1]<=3);zeros=0;continue;}need(b>2);}
            bytes_.push_back(b);zeros=b==0?std::min(zeros+1,2u):0;
        }
    }
    std::uint32_t u(unsigned count){need(count<=32&&count<=bytes_.size()*8-position_);std::uint32_t v=0;for(unsigned i=0;i<count;++i){v=(v<<1)|((bytes_[position_/8]>>(7-position_%8))&1);++position_;}return v;}
    bool bit(){return u(1)!=0;}
    std::uint32_t ue(std::uint32_t maximum=65535){unsigned zeros=0;while(!bit()){need(zeros<31);++zeros;}const auto value=((std::uint64_t(1)<<zeros)-1)+u(zeros);need(value<=maximum);return std::uint32_t(value);}
    int se(int minimum,int maximum){const auto code=ue(2u*std::uint32_t(std::max(-minimum,maximum)));const int value=code&1?int((code+1)/2):-int(code/2);need(value>=minimum&&value<=maximum);return value;}
    void align(){need(bit());while(position_%8)need(!bit());}
    void trailing(){align();need(position_==bytes_.size()*8);}
    bool remaining()const{return position_<bytes_.size()*8;}
};
void profile(Bits& b){
    need(b.u(2)==0);b.bit();need(b.u(5)==1); // Main, no profile-space alias.
    b.u(32);b.u(32);b.u(16);b.u(8); // Compatibility, constraints and level.
}
unsigned ordering(Bits& b){
    b.bit(); // One temporal layer: ordering-info-present does not change count.
    const auto dpb=b.ue(3);need(b.ue(3)==0);b.ue(65535);return dpb; // <=4 DPB pictures; no reordering.
}
void hrd(Bits& b){
    const bool nal=b.bit(),vcl=b.bit();
    if(nal||vcl){need(!b.bit());b.u(4);b.u(4);b.u(5);b.u(5);b.u(5);} // No sub-picture HRD.
    const bool general=b.bit();const bool fixed=general||b.bit();bool low=false;
    if(fixed)b.ue(65535);else low=b.bit();
    const auto count=low?0:b.ue(31);
    for(bool present:{nal,vcl})if(present)for(unsigned i=0;i<=count;++i){b.ue(0x7ffffffe);b.ue(0x7ffffffe);b.bit();}
}
unsigned vps(Bits& b){
    const auto id=b.u(4);need(b.bit()&&b.bit());need(b.u(6)==0&&b.u(3)==0&&b.bit()&&b.u(16)==65535);
    profile(b);ordering(b);need(b.u(6)==0&&b.ue(1023)==0);
    if(b.bit()){need(b.u(32)!=0&&b.u(32)!=0);if(b.bit())b.ue(65535);need(b.ue(1024)==0);}
    need(!b.bit());b.trailing();return id;
}
void vui(Bits& b){
    if(b.bit()){const auto ratio=b.u(8);if(ratio==255)need(b.u(16)!=0&&b.u(16)!=0);}
    if(b.bit())b.bit();
    need(b.bit());b.u(3);need(b.bit()); // Explicit full-range Y, no range remap.
    if(b.bit()){b.u(8);b.u(8);b.u(8);} // Colour labels do not transform the Y plane.
    if(b.bit()){b.ue(5);b.ue(5);}
    b.bit();need(!b.bit());b.bit();need(!b.bit()); // Progressive; no display crop.
    if(b.bit()){need(b.u(32)!=0&&b.u(32)!=0);if(b.bit())b.ue(65535);if(b.bit())hrd(b);}
    if(b.bit()){b.bit();b.bit();b.bit();b.ue(4095);b.ue(16);b.ue(16);b.ue(16);b.ue(16);}
}
// NVENC emits an unused SPS reference set even with every picture forced IDR.
// Parse bounded explicit sets; prediction and entries marked used remain outside
// this contract. No accepted IDR slice can select any reference-picture set.
void unusedReferenceSets(Bits& b,unsigned dpb){
    const auto count=b.ue(64);
    for(unsigned set=0;set<count;++set){
        if(set)need(!b.bit()); // No inter-reference-set prediction.
        const auto negative=b.ue(dpb),positive=b.ue(dpb-negative);
        for(auto entries:{negative,positive}){
            unsigned distance=0;
            for(unsigned i=0;i<entries;++i){distance+=b.ue(32767)+1;need(distance<=32768&&!b.bit());}
        }
    }
}
struct Sequence{unsigned id=0,vps=0,width=0,height=0,ctbs=0;bool sao=false;};
Sequence sps(Bits& b){
    Sequence s;s.vps=b.u(4);need(b.u(3)==0&&b.bit());profile(b);s.id=b.ue(15);
    need(b.ue(3)==1);s.width=b.ue(8192);s.height=b.ue(8192);need(extent(s.width,s.height)&&!b.bit());
    need(b.ue(8)==0&&b.ue(8)==0);b.ue(12);const auto dpb=ordering(b);
    const auto minCb=b.ue(3),diffCb=b.ue(3);need(minCb+diffCb<=3);
    const auto ctb=1u<<(minCb+diffCb+3);s.ctbs=((s.width+ctb-1)/ctb)*((s.height+ctb-1)/ctb);
    const auto minTb=b.ue(3),diffTb=b.ue(3);need(minTb+diffTb<=3);
    b.ue(4);b.ue(4);if(b.bit())need(!b.bit()); // No custom scaling-list payload.
    b.bit();s.sao=b.bit();need(!b.bit()); // No PCM blocks.
    unusedReferenceSets(b,dpb);need(!b.bit()); // No long-term reference pictures.
    b.bit();b.bit();need(b.bit());vui(b);need(!b.bit());b.trailing();return s;
}
struct Picture{unsigned id=0,sps=0,extra=0;bool output=false,sao=false,chromaQp=false,
    wpp=false,loop=false,overrideFilter=false,disabledFilter=false;};
Picture pps(Bits& b){
    Picture p;p.id=b.ue(63);p.sps=b.ue(15);need(!b.bit());p.output=b.bit();p.extra=b.u(3);
    b.bit();b.bit();b.ue(14);b.ue(14);b.se(-26,25);b.bit();b.bit();if(b.bit())b.ue(6);
    b.se(-12,12);b.se(-12,12);p.chromaQp=b.bit();b.bit();b.bit();b.bit();
    need(!b.bit());p.wpp=b.bit();p.loop=b.bit();
    if(b.bit()){p.overrideFilter=b.bit();p.disabledFilter=b.bit();if(!p.disabledFilter){b.se(-6,6);b.se(-6,6);}}
    need(!b.bit());b.bit();b.ue(4);need(!b.bit()&&!b.bit());b.trailing();return p;
}
unsigned idr(Bits& b,const Sequence& s,const Picture& p,bool first,bool& noOutput){
    need(b.bit()==first);const bool flag=b.bit();if(first)noOutput=flag;else need(flag==noOutput);
    need(b.ue(63)==p.id);unsigned address=0;
    if(!first){unsigned bits=0;for(auto last=s.ctbs-1;last;last>>=1)++bits;address=b.u(bits);need(address>0&&address<s.ctbs);}
    for(unsigned i=0;i<p.extra;++i)need(!b.bit());
    need(b.ue(2)==2);if(p.output)need(b.bit()); // Actual I slice, output enabled.
    bool luma=false,chroma=false;if(s.sao){luma=b.bit();chroma=b.bit();}
    b.se(-51,51);if(p.chromaQp){b.se(-12,12);b.se(-12,12);}
    bool disabled=p.disabledFilter;if(p.overrideFilter&&b.bit()){disabled=b.bit();if(!disabled){b.se(-6,6);b.se(-6,6);}}
    if(p.loop&&(luma||chroma||!disabled))b.bit();
    if(p.wpp){const auto count=b.ue(8192);if(count){const auto bits=b.ue(31)+1;for(unsigned i=0;i<count;++i)b.u(bits);}}
    // Entropy is consumed by the real decoder. Neither this header parser nor
    // a decoder returning an unflagged picture certifies complete CABAC syntax.
    // Byte completeness comes from the owned native Lock and signed payload.
    b.align();need(b.remaining());return address;
}
bool startCode(Bytes b,std::size_t from,std::size_t& offset,std::size_t& length){
    for(std::size_t i=from;i+2<b.size;++i)if(!b.data[i]&&!b.data[i+1]){
        if(b.data[i+2]==1){offset=i;length=3;return true;}
        if(i+3<b.size&&!b.data[i+2]&&b.data[i+3]==1){offset=i;length=4;return true;}
    }
    return false;
}
void annexB(Bytes input,unsigned width,unsigned height){
    need(input.data&&input.size&&input.size<=MaximumHevcCoverageBitstreamBytes);
    std::size_t at=0,prefix=0;need(startCode(input,0,at,prefix));for(std::size_t i=0;i<at;++i)need(input.data[i]==0);
    unsigned count=0,phase=0,vpsId=0,slices=0,lastAddress=0,idrType=0;bool noOutput=false;Sequence sequence;Picture picture;
    for(;;){
        need(++count<=8);const auto body=at+prefix;std::size_t next=0,nextPrefix=0;const bool more=startCode(input,body,next,nextPrefix);
        auto end=more?next:input.size;need(end>=body+3);while(end>body+2&&!input.data[end-1])--end;
        const auto h0=input.data[body],h1=input.data[body+1];need(!(h0&0x81)&&h1==1);const unsigned type=(h0>>1)&63;
        Bits b({input.data+body+2,end-body-2});
        if(type==32){need(phase==0);vpsId=vps(b);phase=1;}
        else if(type==33){need(phase==1);sequence=sps(b);need(sequence.vps==vpsId&&sequence.width==width&&sequence.height==height);phase=2;}
        else if(type==34){need(phase==2);picture=pps(b);need(picture.sps==sequence.id);phase=3;}
        else if(type==19||type==20){
            need((phase==3||phase==4)&&++slices<=MaximumHevcCoverageSlices);
            const bool first=phase==3;need(first||type==idrType);
            const auto address=idr(b,sequence,picture,first,noOutput);need(first||address>lastAddress);
            lastAddress=address;idrType=type;phase=4;
        }
        else if(type==35){need(phase==0&&count==1&&b.u(3)==0);b.trailing();}
        else need(false); // Other codecs, P/B pictures, CRA, extra frames and SEI are outside Codec6.
        if(!more)break;
        at=next;prefix=nextPrefix;
    }need(phase==4);
}
} // namespace

bool inspectHevcCoverage(Bytes payload,std::uint16_t width,std::uint16_t height,HevcCoverageView& output) noexcept{
    try{
        need(payload.data&&payload.size>=HevcCoverageHeaderBytes&&payload.size<=HevcCoverageHeaderBytes+MaximumHevcCoverageBitstreamBytes&&extent(width,height));
        const auto* p=payload.data;need(!std::memcmp(p,"SFHC",4)&&p[4]==1&&p[5]==1&&p[6]==1&&p[7]==8);
        need(get(p+8,2)==width&&get(p+10,2)==height&&get(p+12,4)==std::uint32_t(width)*height&&get(p+16,4)==payload.size-HevcCoverageHeaderBytes);
        need(p[20]==1&&p[21]==1&&p[22]==1);for(unsigned i=23;i<HevcCoverageHeaderBytes;++i)need(p[i]==0);
        const Bytes coded{p+HevcCoverageHeaderBytes,payload.size-HevcCoverageHeaderBytes};annexB(coded,width,height);
        output={coded,width,height};return true;
    }catch(...){return false;}
}
bool wrapHevcCoverage(Bytes coded,std::uint16_t width,std::uint16_t height,std::vector<std::uint8_t>& output) noexcept{
    try{
        need(extent(width,height));annexB(coded,width,height);
        std::vector<std::uint8_t> result(HevcCoverageHeaderBytes+coded.size,0);auto* p=result.data();
        std::memcpy(p,"SFHC",4);p[4]=1;p[5]=1;p[6]=1;p[7]=8;put(p+8,width,2);put(p+10,height,2);
        put(p+12,std::uint32_t(width)*height,4);put(p+16,std::uint32_t(coded.size),4);p[20]=1;p[21]=1;p[22]=1;
        std::copy_n(coded.data,coded.size,p+HevcCoverageHeaderBytes);output.swap(result);return true;
    }catch(...){return false;}
}
} // namespace stream_bridge
