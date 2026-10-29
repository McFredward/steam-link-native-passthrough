#include "bridge/stream_bridge_hevc_coverage.h"
#include "bridge/stream_bridge_frame.h"
#include "stream_bridge_hevc_fixture.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace sb=stream_bridge;
namespace {
unsigned checks=0;
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
sb::Bytes bytes(const std::vector<std::uint8_t>& v){return {v.data(),v.size()};}
struct Writer{
    std::vector<std::uint8_t> out;unsigned position=0;
    void u(std::uint32_t value,unsigned count){for(unsigned i=count;i;--i){if(!(position%8))out.push_back(0);out.back()|=((value>>(i-1))&1)<<(7-position%8);++position;}}
    void bit(bool value){u(value,1);}
    void ue(unsigned value){++value;unsigned bits=0;for(auto v=value;v;v>>=1)++bits;u(0,bits-1);u(value,bits);}
    void end(){bit(true);while(position%8)bit(false);}
};
struct Options{unsigned profile=1,chroma=1,depth=0,width=64,height=64,layers=0,dpb=0,reorder=0,rps=0,slice=2,ppsId=0;
    bool crop=false,full=true,first=true,tiles=false;};
void ptl(Writer& w,const Options& o){w.u(0,2);w.bit(false);w.u(o.profile,5);w.u(0,32);w.u(0,32);w.u(0,16);w.u(30,8);}
void ordering(Writer&w,const Options&o){w.bit(false);w.ue(o.dpb);w.ue(o.reorder);w.ue(0);}
void nal(std::vector<std::uint8_t>& au,unsigned type,const Writer&w){
    au.insert(au.end(),{0,0,0,1,std::uint8_t(type<<1),1});unsigned zeros=0;
    for(auto b:w.out){if(zeros==2&&b<=3){au.push_back(3);zeros=0;}au.push_back(b);zeros=b==0?std::min(zeros+1,2u):0;}
}
// Syntax-only generators exercise parser branches. Their placeholder CABAC
// bytes are deliberately not claimed to decode; the real fixture above does.
std::vector<std::uint8_t> syntax(const Options& o={}){
    std::vector<std::uint8_t> result;Writer v;v.u(0,4);v.bit(true);v.bit(true);v.u(0,6);v.u(o.layers,3);v.bit(true);v.u(65535,16);ptl(v,o);ordering(v,o);v.u(0,6);v.ue(0);v.bit(false);v.bit(false);v.end();nal(result,32,v);
    Writer s;s.u(0,4);s.u(o.layers,3);s.bit(true);ptl(s,o);s.ue(0);s.ue(o.chroma);s.ue(o.width);s.ue(o.height);s.bit(o.crop);s.ue(o.depth);s.ue(o.depth);s.ue(0);ordering(s,o);
    s.ue(0);s.ue(3);s.ue(0);s.ue(3);s.ue(0);s.ue(0);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.ue(o.rps);s.bit(false);s.bit(false);s.bit(false);s.bit(true);
    s.bit(false);s.bit(false);s.bit(true);s.u(5,3);s.bit(o.full);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.bit(false);s.end();nal(result,33,s);
    Writer p;p.ue(o.ppsId);p.ue(0);p.bit(false);p.bit(false);p.u(0,3);p.bit(false);p.bit(false);p.ue(0);p.ue(0);p.ue(0);p.bit(false);p.bit(false);p.bit(false);p.ue(0);p.ue(0);
    for(unsigned i=0;i<4;++i){p.bit(false);}
    p.bit(o.tiles);for(unsigned i=0;i<5;++i){p.bit(false);}
    p.ue(0);p.bit(false);p.bit(false);p.end();nal(result,34,p);
    Writer i;i.bit(o.first);i.bit(true);i.ue(0);i.ue(o.slice);i.ue(0);i.end();i.u(128,8);nal(result,20,i);return result;
}
void rejected(const std::vector<std::uint8_t>& coded,const char* why){std::vector<std::uint8_t> output{91};check(!sb::wrapHevcCoverage(bytes(coded),64,64,output)&&output==std::vector<std::uint8_t>{91},why);}
void parser(){
    const std::vector<std::uint8_t> real(HevcMainIdrFixture.begin(),HevcMainIdrFixture.end());
    std::vector<std::uint8_t> wrapped;check(sb::wrapHevcCoverage(bytes(real),64,64,wrapped),"real CPU-libx265 Main8bit420 IDR parses");
    sb::HevcCoverageView view;check(sb::inspectHevcCoverage(bytes(wrapped),64,64,view)&&view.width==64&&view.height==64&&view.annexB.size==real.size()&&std::equal(real.begin(),real.end(),view.annexB.data),"view points at exact original owned HEVC bytes");
    auto unchanged=view;
    for(unsigned offset=0;offset<sb::HevcCoverageHeaderBytes;++offset){auto changed=wrapped;changed[offset]^=128;
        check(!sb::inspectHevcCoverage(bytes(changed),64,64,view)&&view.annexB.data==unchanged.annexB.data&&view.width==unchanged.width,"all header fields/reserved bytes are checked transactionally");}
    for(unsigned length=0;length<92;++length){std::vector<std::uint8_t> truncated(real.begin(),real.begin()+length);
        // The CABAC body needs a real decoder; every earlier header truncation
        // is refused here. The final four bytes are intentionally not asserted.
        rejected(truncated,"truncated framing or parameter/slice header refused");}
    check(!sb::inspectHevcCoverage(bytes(wrapped),66,64,view),"signed outer width must equal actual SPS extent");
    check(!sb::inspectHevcCoverage(bytes(wrapped),64,66,view),"signed outer height must equal actual SPS extent");
    auto doubled=real;doubled.insert(doubled.end(),real.begin(),real.end());rejected(doubled,"second independently encoded picture is not one mask");
    for(unsigned type:{0u,1u,16u,18u,21u,22u,39u,40u,63u}){auto changed=real;changed[87]=std::uint8_t(type<<1);rejected(changed,"P/B/CRA/unknown NAL cannot replace the actual IDR");}
    for(unsigned offset:{4u,32u,77u,87u}){auto changed=real;changed[offset]|=128;rejected(changed,"forbidden NAL bit refused");changed=real;changed[offset+1]=9;rejected(changed,"foreign layer refused");changed[offset+1]=2;rejected(changed,"nonzero temporal layer refused");}
    auto h264=std::vector<std::uint8_t>{0,0,0,1,0x67,0x42,0,0x1e,0,0,0,1,0x65,0x88,0x84};rejected(h264,"AnnexB H264 is not HEVC");
    check(sb::wrapHevcCoverage(bytes(syntax()),64,64,wrapped),"syntax-only bounded parameter-set fixture exercises independent IDR branch");
    for(unsigned field=0;field<13;++field){Options o;switch(field){case 0:o.profile=4;break;case 1:o.chroma=3;break;case 2:o.depth=2;break;case 3:o.width=8193;break;case 4:o.height=66;break;case 5:o.layers=1;break;case 6:o.dpb=4;break;case 7:o.reorder=1;break;case 8:o.rps=1;break;case 9:o.crop=true;break;case 10:o.full=false;break;case 11:o.slice=1;break;case 12:o.ppsId=1;break;}
        rejected(syntax(o),"unsupported profile/8bit/chroma/extent/DPB/reorder/RPS/range/slice/PPS mismatch refused");}
    Options o;o.first=false;rejected(syntax(o),"dependent/nonfirst IDR slice refused");o={};o.tiles=true;rejected(syntax(o),"tiles are outside the initial single-slice contract");
    auto huge=real;huge.resize(sb::MaximumHevcCoverageBitstreamBytes+1);rejected(huge,"compressed input bound enforced before allocation");
}
void authentication(){
    sb::Pairing pair;pair.role=sb::Role::Producer;pair.pairId.fill(23);pair.key.fill(19);sb::Nonce binding{},connection{};binding.fill(7);connection.fill(9);
    sb::AuthSession producer,receiver;check(producer.prepareProducer(pair,binding)==sb::AuthStatus::Ok,"actual producer prepared");pair.role=sb::Role::Receiver;check(receiver.prepareReceiver(pair,{connection,1})==sb::AuthStatus::Ok,"independent authenticated receiver prepared");
    const std::vector<std::uint8_t> coded(HevcMainIdrFixture.begin(),HevcMainIdrFixture.end()),rgb{0,0,0,1,0x26,1,0x80,0x31};
    std::vector<std::uint8_t> mask,complete;check(sb::wrapHevcCoverage(bytes(coded),64,64,mask),"real mask wrapped");
    check(sb::appendCertifiedFrame(bytes(rgb),{51,17,64,64},sb::CoverageEncoding::HevcIntra,bytes(mask),producer,complete)==sb::FrameStatus::Ok,"actual original RGB AU and encoded mask jointly certified");
    sb::CertifiedAccessUnit certificate;check(sb::inspectCertifiedFrame(bytes(complete),receiver,certificate)==sb::FrameStatus::Ok&&certificate.original==rgb&&certificate.descriptor.sourceFrame==51&&certificate.descriptor.encoderGeneration==17&&certificate.originalAccessUnitVerified,"encoded HEVC does not weaken exact original AU/source binding");
    for(unsigned i=0;i<complete.size();++i){auto changed=complete;changed[i]^=1;sb::CertifiedAccessUnit unchanged;unchanged.descriptor.sourceFrame=999;
        check(sb::inspectCertifiedFrame(bytes(changed),receiver,unchanged)!=sb::FrameStatus::Ok&&unchanged.descriptor.sourceFrame==999,"mutated AU/certificate/payload never publishes new proof");}
    std::vector<std::uint8_t> metadata;check(sb::serializeCoverageMetadata(certificate,metadata),"authenticated local metadata retains exact encoded payload");
    std::vector<std::uint8_t> samples{43};sb::CoverageRange range{11,12};
    check(!sb::decodeCertifiedCoverage(certificate,samples,range)&&samples==std::vector<std::uint8_t>{43}&&range.minimum==11,"portable parser never pretends to decode HEVC pixels");
    sb::CertifiedAccessUnit out;out.descriptor.sourceFrame=999;
    check(sb::inspectCoverageMetadataDecoded(bytes(metadata),receiver,out,samples,range)==sb::FrameStatus::Malformed&&out.descriptor.sourceFrame==999&&samples==std::vector<std::uint8_t>{43},"legacy sample API cannot promote HEVC header validation into decoded pixels");
    check(sb::appendCertifiedFrame(bytes(rgb),{51,17,66,64},sb::CoverageEncoding::HevcIntra,bytes(mask),producer,complete)==sb::FrameStatus::InvalidArgument,"outer signed descriptor cannot relabel a different coded mask extent");
}
}
int main(){try{parser();authentication();std::printf("%u bounded HEVC envelope/authentication checks passed\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
