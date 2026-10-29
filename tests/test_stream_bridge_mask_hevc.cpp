#include "receiver/stream_bridge_mask_hevc.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
namespace hevc=stream_bridge_mask_hevc;
namespace sb=stream_bridge;
namespace fs=std::filesystem;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
sb::Bytes bytes(const std::vector<std::uint8_t>& b){return {b.data(),b.size()};}
std::vector<std::uint8_t> read(const fs::path& path){check(fs::file_size(path)<=sb::MaximumHevcCoverageBitstreamBytes,"fixture stays bounded");std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
hevc::Input input(sb::Bytes payload){hevc::Input in;in.source.connection[2]=7;in.source.generation=9;in.descriptor={41,11,64,64,1};in.binding[3]=8;in.payload=payload;return in;}
std::vector<std::uint8_t> unchecked(const std::vector<std::uint8_t>& coded,unsigned width=64,unsigned height=64){
    std::vector<std::uint8_t> out(32,0);std::copy_n("SFHC",4,out.begin());out[4]=out[5]=out[6]=1;out[7]=8;
    const auto put=[&](unsigned offset,unsigned value,unsigned size){for(unsigned i=0;i<size;++i)out[offset+i]=std::uint8_t(value>>(i*8));};
    put(8,width,2);put(10,height,2);put(12,width*height,4);put(16,coded.size(),4);out[20]=out[21]=out[22]=1;out.insert(out.end(),coded.begin(),coded.end());return out;
}
struct Nal {std::size_t start=0,body=0,end=0;unsigned type=0;};
std::vector<Nal> nals(const std::vector<std::uint8_t>& coded){
    std::vector<Nal> result;for(std::size_t i=0;i+3<coded.size();++i)if(!coded[i]&&!coded[i+1]&&(coded[i+2]==1||(coded[i+2]==0&&coded[i+3]==1))){
        const auto body=i+(coded[i+2]==1?3:4);if(!result.empty())result.back().end=i;result.push_back({i,body,coded.size(),unsigned((coded[body]>>1)&63)});i=body+1;
    }return result;
}
void unavailable(const hevc::Libraries& libs){
    hevc::Decoder decoder;hevc::DecodedMask output;output.samples={37};
    check(!decoder.ready()&&decoder.decodeIntra({},output)==hevc::Status::Unavailable&&output.samples==std::vector<std::uint8_t>{37},"default off preserves caller output");
    check(!decoder.prepareCold({libs.utility,libs.utility})&&!decoder.ready(),"wrong library symbols cannot become a decoder ABI");
    check(!decoder.prepareCold({"/nonexistent-sfnp-codec","/nonexistent-sfnp-utility"})&&!decoder.ready(),"missing exact dependency does not fall back to a system decoder");
}
void decode(const hevc::Libraries& libs,const fs::path& root){
    hevc::Decoder decoder;check(decoder.prepareCold(libs)&&decoder.ready(),"explicit matching public codec ABI loaded");
    hevc::DecodedMask output;std::vector<std::uint8_t> held;
    for(const auto* name:{"zero","opaque","codes","slices","tall-slices"}){
        const std::string fixture=name;const bool tall=fixture=="tall-slices",multi=tall||fixture=="slices";
        const unsigned width=tall?1152:(multi?256:64),height=tall?4608:(multi?256:64);
        const auto coded=read(root/(fixture+".hevc"));std::vector<std::uint8_t> payload;
        if(multi){const auto units=nals(coded);check(std::count_if(units.begin(),units.end(),[](const auto& n){return n.type==19||n.type==20;})==2,"actual CPU fixture has two IDR slices rather than two pictures");}
        check(sb::wrapHevcCoverage(bytes(coded),width,height,payload),"real software Main/full-range/one-picture fixture passes normative envelope proof");
        auto in=input(bytes(payload));in.descriptor.codedWidth=width;in.descriptor.codedHeight=height;const auto started=std::chrono::steady_clock::now();const auto status=decoder.decodeIntra(in,output);
        if(status!=hevc::Status::Ok)std::fprintf(stderr,"%s decode refused: %s\n",name,hevc::statusName(status));
        check(status==hevc::Status::Ok&&output.samples.size()==std::size_t(width)*height,"actual decoder produces exactly one full A8 extent");
        check(output.source.connection==in.source.connection&&output.source.generation==in.source.generation&&output.descriptor.sourceFrame==41&&output.descriptor.encoderGeneration==11&&output.binding==in.binding,"decoded output retains exact source/frame/epoch/binding rather than a latest identity");
        if(std::string(name)=="zero")check(std::all_of(output.samples.begin(),output.samples.end(),[](auto b){return b==0;})&&output.range.minimum==0&&output.range.maximum==0,"zero luma remains zero coverage");
        else if(std::string(name)=="opaque")check(std::all_of(output.samples.begin(),output.samples.end(),[](auto b){return b==255;})&&output.range.minimum==255&&output.range.maximum==255,"255 luma remains opaque coverage");
        else{
            unsigned error=0;for(unsigned i=0;i<output.samples.size();++i)error=std::max(error,unsigned(std::abs(int(output.samples[i])-int(i%256))));
            check(error<=2&&output.range.minimum==0&&output.range.maximum==255,"all256 alpha codes expose accidental limited-range/RGB scaling; QP0 fixture is still explicitly lossy");if(fixture=="codes")held=payload;
        }
        std::printf("fixture=%s decode-us=%lld bytes=%zu\n",name,static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count()),coded.size());
    }
    const auto refusal=[&](const std::vector<std::uint8_t>& payload,unsigned width=64){auto in=input(bytes(payload));in.descriptor.codedWidth=std::uint16_t(width);const auto before=output.samples;const auto id=output.descriptor.sourceFrame;check(decoder.decodeIntra(in,output)!=hevc::Status::Ok&&output.samples==before&&output.descriptor.sourceFrame==id,"bad codec input cannot partially publish samples or change frame identity");};
    refusal(unchecked(read(root/"limited.hevc")));refusal(unchecked(read(root/"crop.hevc"),62),62);
    const auto coded=read(root/"codes.hevc");const auto units=nals(coded);auto missing=coded;
    for(const auto& unit:units)if(unit.type==34){missing.erase(missing.begin()+unit.start,missing.begin()+unit.end);break;}
    refusal(unchecked(missing)); // Prior successful decode cannot supply missing PPS.
    auto truncated=coded;for(const auto& unit:units)if(unit.type==19||unit.type==20){truncated.resize(unit.body+3);break;}
    refusal(unchecked(truncated)); // Full entropy decode, not just envelope syntax.
    auto extra=coded;extra.insert(extra.end(),coded.begin(),coded.end());refusal(unchecked(extra));
    auto dependent=coded;for(const auto& unit:units)if(unit.type==19||unit.type==20)dependent[unit.body]=2;refusal(unchecked(dependent));
    auto in=input(bytes(held));in.source.connection={};const auto before=output.samples;
    check(decoder.decodeIntra(in,output)==hevc::Status::InvalidIdentity&&output.samples==before,"missing source identity never publishes decoded coverage");
    in=input(bytes(held));in.descriptor.sourceFrame=0;check(decoder.decodeIntra(in,output)==hevc::Status::InvalidIdentity,"zero submission refuses");
    in=input(bytes(held));in.descriptor.codedWidth=32;check(decoder.decodeIntra(in,output)==hevc::Status::InvalidEnvelope,"signed extent cannot be replaced by decoder dimensions");
    in=input(bytes(held));in.source.generation=27;in.descriptor.encoderGeneration=31;in.descriptor.sourceFrame=91;in.binding[3]=22;
    check(decoder.decodeIntra(in,output)==hevc::Status::Ok&&output.source.generation==27&&output.descriptor.encoderGeneration==31&&output.descriptor.sourceFrame==91&&output.binding[3]==22,"fresh independent AU recovers after bad payload and source restart without old decoder state");
    decoder.stopCold();check(!decoder.ready()&&decoder.decodeIntra(in,output)==hevc::Status::Unavailable,"cold stop revokes decoder availability");
}
}
int main(int argc,char** argv){try{if(argc!=4){std::fprintf(stderr,"Usage: test_stream_bridge_mask_hevc CODEC_LIBRARY UTILITY_LIBRARY FIXTURE_DIRECTORY\n");return 2;}const hevc::Libraries libraries{fs::absolute(argv[1]),fs::absolute(argv[2])};unavailable(libraries);decode(libraries,argv[3]);std::printf("%u real HEVC mask decoder checks passed\n",checks);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
