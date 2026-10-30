#include "receiver/stream_bridge_mask_hevc.h"
#include "receiver/stream_bridge_mask_hevc_pool.h"
#include <thread>
#include <set>
#include "bridge/stream_bridge_hevc_coverage.h"
#include "pc/stream-bridge-mask-hevc-parameters.h"
#include "receiver/sha256.h"
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
void temporal(const hevc::Libraries& libs,const fs::path& root){
    hevc::Decoder decoder;check(decoder.prepareCold(libs),"temporal decoder prepared from owned public ABI");
    std::array<std::vector<std::uint8_t>,16> payloads;
    sb::sender::MaskHevcParameters parameters;parameters.reset(71);
    for(unsigned n=0;n<16;++n){char name[40];std::snprintf(name,sizeof name,"temporal-%03u.hevc",n);const auto original=read(root/name);auto coded=original;
        if(n%8)for(const auto& nal:nals(original))if(nal.type==1){coded.erase(coded.begin(),coded.begin()+nal.start);break;}
        const sb::sender::MaskHevcPicture picture{71,10*(n+1),n%8?10*n:0,10*((n/8)*8+1),n%8==0};
        check(parameters.complete(coded,picture)==(n%8?sb::sender::MaskHevcParameters::Result::Restored:sb::sender::MaskHevcParameters::Result::KeyCached)&&coded==original,"driver-style omitted P headers are repaired with identical source bytes before real entropy decode");
        check(sb::wrapHevcTemporalCoverage(bytes(coded),64,64,71,10*(n+1),n%8?10*n:0,n%8==0,10*((n/8)*8+1),payloads[n]),"real IPPP8 Main8 AU passes bounded temporal grammar");
        std::vector<std::uint8_t> old;check(!sb::wrapHevcCoverage(bytes(coded),64,64,old)||n%8==0,"dependent P remains outside legacy Codec6");}
    for(unsigned offset=56;offset<64;++offset){auto invalid=payloads[1];invalid[offset]=1;sb::HevcCoverageView view;
        check(!sb::inspectHevcTemporalCoverage(bytes(invalid),64,64,view),"temporal reserved envelope bytes remain closed");}
    for(unsigned offset:{0u,4u,5u,6u,7u,8u,10u,12u,16u,20u,21u,22u,23u}){auto invalid=payloads[1];invalid[offset]^=128;sb::HevcCoverageView view;
        check(!sb::inspectHevcTemporalCoverage(bytes(invalid),64,64,view),"temporal codec/profile/range/extent/count/kind are checked before decode");}
    {auto invalid=payloads[1];std::fill_n(invalid.begin()+24,8,0);sb::HevcCoverageView view;
        check(!sb::inspectHevcTemporalCoverage(bytes(invalid),64,64,view),"zero temporal epoch cannot alias a prior session");
        invalid=payloads[1];std::fill_n(invalid.begin()+40,8,0);
        check(!sb::inspectHevcTemporalCoverage(bytes(invalid),64,64,view),"P cannot omit its predecessor");
        invalid=payloads[1];invalid[48]=255;
        check(!sb::inspectHevcTemporalCoverage(bytes(invalid),64,64,view),"future group anchor cannot describe this P picture");}
    hevc::DecodedMask output;
    const auto frame=[&](unsigned n){auto in=input(bytes(payloads[n]));in.descriptor.sourceFrame=100+n;return in;};
    check(decoder.decodeTemporal(frame(1),output)==hevc::Status::MissingReference,"P cannot create reference state without its authenticated key");
    for(unsigned n=0;n<16;++n){
        const auto status=decoder.decodeTemporal(frame(n),output);
        if(status!=hevc::Status::Ok)std::fprintf(stderr,"temporal %u: %s\n",n,hevc::statusName(status));
        check(status==hevc::Status::Ok&&output.descriptor.sourceFrame==100+n,"exact packet identity survives IP prediction without decoder draining/reordering");
        char name[40];std::snprintf(name,sizeof name,"temporal-%03u.r8",n);const auto expected=read(root/name);
        unsigned error=0;for(unsigned i=0;i<4096;++i)error=std::max(error,unsigned(std::abs(int(expected[i])-int(output.samples[i]))));
        check(error<=3,"real decoded P coverage matches its own current source samples");
    }
    check(decoder.statistics().contextsOpened==1,"one persistent DPB decodes both complete IPPP8 groups");
    decoder.invalidateTemporal();check(decoder.decodeTemporal(frame(0),output)==hevc::Status::Ok,"key restores invalidated codec state");
    const auto held=output.samples;
    check(decoder.decodeTemporal(frame(2),output)==hevc::Status::MissingReference&&output.samples==held,"missing dependency cannot publish partial or previous mask");
    check(decoder.decodeTemporal(frame(3),output)==hevc::Status::MissingReference,"dependent tail stays refused after a gap");
    check(decoder.decodeTemporal(frame(8),output)==hevc::Status::Ok&&decoder.decodeTemporal(frame(9),output)==hevc::Status::Ok,"next independent key restores only the codec, with current-frame proof");
    auto wrong=frame(10);wrong.binding[0]^=1;
    check(decoder.decodeTemporal(wrong,output)==hevc::Status::MissingReference,"fresh source binding cannot borrow old DPB");
    check(decoder.decodeTemporal(frame(11),output)==hevc::Status::MissingReference,"old chain remains invalid after source change");
    check(decoder.decodeTemporal(frame(8),output)==hevc::Status::Ok,"complete key recovers after wrong binding");
    decoder.invalidateTemporal();check(decoder.decodeTemporal(frame(9),output)==hevc::Status::MissingReference,"explicit cancellation withdraws all prediction state");
}

void temporalPool(const hevc::Libraries& libs,const fs::path& root,bool lanes=false){
    hevc::Pool pool;check(pool.prepareCold(libs),"bounded group-affine pool prepared");
    std::array<std::vector<std::uint8_t>,16> payloads;
    for(unsigned n=0;n<16;++n){const unsigned picture=lanes?n/2:n;char name[40];std::snprintf(name,sizeof name,"temporal-%03u.hevc",picture);const auto coded=read(root/name);
        check(sb::wrapHevcTemporalCoverage(bytes(coded),64,64,81+(lanes?n%2:0),picture+1,picture%8?picture:0,picture%8==0,(picture/8)*8+1,payloads[n]),"real temporal pool fixture wrapped");}
    unsigned sent=0,received=0;std::set<std::uint64_t> frames;
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(received<16&&std::chrono::steady_clock::now()<end){
        while(sent<16){auto in=input(bytes(payloads[sent]));in.descriptor.sourceFrame=100+sent;if(!pool.submit(in))break;++sent;}
        hevc::Pool::Result result;
        while(pool.take(result)){check(result.status==hevc::Status::Ok&&result.decoded.descriptor.sourceFrame==result.identity.descriptor.sourceFrame,"parallel groups retain serial references and exact completed frame identity");
            check(frames.insert(result.identity.descriptor.sourceFrame).second,"each queued P/key picture completes once");++received;}
        std::this_thread::yield();
    }
    check(sent==16&&received==16,"both independent GOPs make progress through fixed eight slots");
    pool.cancel();auto in=input(bytes(payloads[lanes?2:1]));in.descriptor.sourceFrame=200;
    check(pool.submit(in),"post-cancel dependent input remains an authenticated job");
    bool refused=false;
    while(!refused&&std::chrono::steady_clock::now()<end){hevc::Pool::Result result;if(pool.take(result)){check(result.status==hevc::Status::MissingReference,"cancel cannot borrow a previously decoded same-group DPB");refused=true;}else std::this_thread::yield();}
    check(refused,"post-cancel missing-reference status completes without blocking");
}

void reuse(const hevc::Libraries& libs,const fs::path& root){
    hevc::Decoder decoder;check(decoder.prepareCold(libs),"reusable decoder prepared");
    std::array<std::vector<std::uint8_t>,3> payloads;
    unsigned n=0;for(const auto* name:{"zero","opaque","codes"}){const auto coded=read(root/(std::string(name)+".hevc"));check(sb::wrapHevcCoverage(bytes(coded),64,64,payloads[n++]),"complete independent fixture wrapped");}
    hevc::DecodedMask output;
    for(unsigned i=0;i<24;++i){auto in=input(bytes(payloads[i%3]));in.descriptor.sourceFrame=100+i;in.descriptor.cameraRecovery=i%3;
        check(decoder.decodeIntra(in,output)==hevc::Status::Ok&&output.descriptor.sourceFrame==100+i&&output.descriptor.cameraRecovery==i%3,"reused context cannot return prior frame or policy after EOF/flush");
        const auto expected=i%3==0?0:(i%3==1?255:0);check(i%3==2?output.samples[0]<=2:output.samples[0]==expected,"alternating zero/opaque/fractional IDRs do not borrow reference pixels");}
    const auto stats=decoder.statistics();check(stats.contextsOpened==1&&stats.attempts==24&&stats.decoded==24,"one decoder allocation serves repeated independent same-epoch IDRs");
    check(stats.sampleBufferGrows==2&&stats.sampleBufferReuses==22,"transactional alternating output buffers stop full R8 storage growth after two successful pictures");
    const auto coded=read(root/"codes.hevc");auto missing=coded;
    for(const auto& unit:nals(coded))if(unit.type==34){missing.erase(missing.begin()+unit.start,missing.begin()+unit.end);break;}
    const auto invalid=unchecked(missing);auto absent=input(bytes(invalid));const auto before=output.samples;
    check(decoder.decodeIntra(absent,output)==hevc::Status::InvalidEnvelope&&output.samples==before,"retained same-extent context cannot supply a missing PPS from an earlier packet");
    auto recovered=input(bytes(payloads[1]));recovered.descriptor.sourceFrame=200;
    check(decoder.decodeIntra(recovered,output)==hevc::Status::Ok&&output.range.minimum==255&&decoder.statistics().contextsOpened==1,"valid independent opaque IDR recovers without cached fractional pixels or a context rebuild");
    auto in=input(bytes(payloads[0]));in.source.generation=13;in.descriptor.encoderGeneration=17;
    check(decoder.decodeIntra(in,output)==hevc::Status::Ok&&decoder.statistics().contextsOpened==2&&output.source.generation==13,"source/encoder epoch change creates fresh bounded decoder state");
    decoder.stopCold();check(decoder.prepareCold(libs)&&decoder.decodeIntra(in,output)==hevc::Status::Ok&&decoder.statistics().contextsOpened==3,"cold stop frees resources before explicit reprepare");
}
void pool(const hevc::Libraries& libs,const fs::path& root){
    hevc::Pool pool;check(!pool.ready()&&!pool.prepareCold({libs.utility,libs.utility}),"unprepared/wrong-ABI pool cannot enable decoding");
    check(pool.prepareCold(libs)&&pool.ready(),"four explicitly prepared decoder workers start");
    const auto coded=read(root/"slices.hevc");std::vector<std::uint8_t> payload;check(sb::wrapHevcCoverage(bytes(coded),256,256,payload),"real independent multi-slice fixture wrapped for bounded pool");
    auto in=input(bytes(payload));in.descriptor.codedWidth=in.descriptor.codedHeight=256;
    for(unsigned i=0;i<hevc::Pool::Capacity;++i){in.descriptor.sourceFrame=i+1;check(pool.submit(in),"fixed pool owns each queued input");}
    check(!pool.submit(in),"completed-but-unconsumed slots still enforce the exact fixed capacity");
    std::fill(payload.begin(),payload.end(),0); // Borrowed caller bytes may disappear immediately.
    std::set<std::uint64_t> frames;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(frames.size()<hevc::Pool::Capacity&&std::chrono::steady_clock::now()<end){
        hevc::Pool::Result result;if(!pool.take(result)){std::this_thread::yield();continue;}
        check(result.status==hevc::Status::Ok&&result.identity.payload.data==nullptr&&result.decoded.samples.size()==65536,"workers decode owned copies and return no borrowed packet pointer");
        check(result.identity.descriptor.sourceFrame==result.decoded.descriptor.sourceFrame&&frames.insert(result.decoded.descriptor.sourceFrame).second,"out-of-order completion preserves each exact frame once");
        check(result.decoded.range.minimum==0&&result.decoded.range.maximum==255,"multi-slice fractional luma survives independent worker contexts");
    }
    check(frames.size()==hevc::Pool::Capacity,"all fixed outstanding jobs complete without a mask substitution");
    check(pool.statistics().contextsOpened<=hevc::Pool::Workers&&pool.statistics().decoded==hevc::Pool::Capacity,"bounded workers reuse at most one context each");
    check(sb::wrapHevcCoverage(bytes(coded),256,256,payload),"restore caller-owned packet");in.payload=bytes(payload);
    hevc::Pool::Result recycled;unsigned sent=0,receivedReuse=0;
    const auto reuseEnd=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(receivedReuse<64&&std::chrono::steady_clock::now()<reuseEnd){
        while(sent<64){in.descriptor.sourceFrame=300+sent;if(!pool.submit(in))break;++sent;}
        while(pool.take(recycled)){
            check(recycled.status==hevc::Status::Ok&&recycled.decoded.descriptor.sourceFrame==recycled.identity.descriptor.sourceFrame,"recycled R8 storage retains exact fresh decoded identity");
            for(std::size_t i=0;i<recycled.decoded.samples.size();++i)check(std::abs(int(recycled.decoded.samples[i])-int(i%256))<=2,"every reused fractional-alpha byte still matches the same owned source");
            ++receivedReuse;
        }
        std::this_thread::yield();
    }
    const auto reused=pool.statistics();
    check(receivedReuse==64&&reused.sampleBufferReuses>32&&reused.sampleBufferGrows<=2*(hevc::Pool::Capacity+hevc::Pool::Workers+1),"persistent consumer returns full R8 buffers to the bounded pool instead of per-picture growth");
    check(reused.started==reused.collected&&reused.collected==hevc::Pool::Capacity+64,"queue and collection timings account for every completed pool input");
    for(unsigned i=0;i<hevc::Pool::Capacity;++i){in.descriptor.sourceFrame=100+i;check(pool.submit(in),"old source work queued before revocation");}
    pool.cancel();in.source.generation=99;in.binding[0]=101;in.descriptor.sourceFrame=999;
    const auto retryEnd=std::chrono::steady_clock::now()+std::chrono::seconds(5);bool submitted=false;
    while(!(submitted=pool.submit(in))&&std::chrono::steady_clock::now()<retryEnd)std::this_thread::yield();
    check(submitted,"new source can submit after bounded running old jobs retire");
    bool received=false;
    while(!received&&std::chrono::steady_clock::now()<retryEnd){hevc::Pool::Result result;if(!pool.take(result)){std::this_thread::yield();continue;}
        check(result.status==hevc::Status::Ok&&result.decoded.source.generation==99&&result.decoded.descriptor.sourceFrame==999&&result.decoded.binding[0]==101,"revocation cannot publish queued/completed/running old-source results");received=true;}
    check(received,"new source completes after revocation");pool.stopCold();hevc::Pool::Result result;
    check(!pool.ready()&&!pool.submit(in)&&!pool.take(result),"explicit stop joins workers and revokes every outstanding result");
}

std::string pixelDigest(const std::vector<std::uint8_t>& pixels){
    frame_receiver::Sha256 hash;hash.update(pixels.data(),pixels.size());const auto digest=hash.finish();char hex[65]{};
    for(unsigned n=0;n<digest.size();++n)std::snprintf(hex+2*n,3,"%02x",unsigned(digest[n]));
    return hex;
}
void independentSlices(const hevc::Libraries& libs,const fs::path& root,unsigned count,const char* variant=nullptr){
    const auto name=std::string("independent-slices-")+(variant?variant:std::to_string(count));
    const auto directory=root/name;
    std::array<std::vector<std::uint8_t>,16> coded,payloads;std::array<std::string,16> reference;
    std::ifstream hashes(directory/"reference.sha256");
    for(unsigned n=0;n<16;++n){
        char name[40];std::snprintf(name,sizeof name,"nal-%03u.hevc",n);coded[n]=read(directory/name);hashes>>reference[n];
        check(reference[n].size()==64,"owned encoder reconstruction hash is complete");
        const auto units=nals(coded[n]);check(std::count_if(units.begin(),units.end(),[](const auto& nal){return nal.type==1||nal.type==19||nal.type==20;})==count,"actual independent-slice count matches owned fixture");
        check(sb::wrapHevcTemporalCoverage(bytes(coded[n]),256,256,81,n+1,n%8?n:0,n%8==0,(n/8)*8+1,payloads[n]),"independent non-WPP slices satisfy unchanged temporal contract");
    }
    const auto frame=[&](unsigned n){auto in=input(bytes(payloads[n]));in.descriptor.codedWidth=in.descriptor.codedHeight=256;in.descriptor.sourceFrame=100+n;return in;};
    hevc::Decoder decoder;check(decoder.prepareCold(libs),"private parallel backend prepared");hevc::DecodedMask output;
    for(unsigned n=0;n<16;++n){
        check(decoder.decodeTemporal(frame(n),output)==hevc::Status::Ok,"independent slices return one immediate current picture");
        check(output.descriptor.sourceFrame==100+n&&output.source.connection==frame(n).source.connection&&output.binding==frame(n).binding,"slice execution preserves exact public packet identity");
        check(pixelDigest(output.samples)==reference[n],"every decoded alpha byte equals the complete reference reconstruction");
    }
    const auto stats=decoder.statistics();
    check(stats.sliceBatches>=14&&stats.sliceJobs==count*stats.sliceBatches,"real independent-slice jobs execute through the new codec path, without frame buffering");
    std::printf("fixture=%s verified-pictures=16; slice batches/jobs=%llu/%llu; exact full Y SHA256\n",name.c_str(),static_cast<unsigned long long>(stats.sliceBatches),static_cast<unsigned long long>(stats.sliceJobs));
    if(count!=2||variant)return;
    // Two header-valid slices, but the second terminates after a quarter of
    // the picture. The first slice is valid; no partial alpha may escape.
    const auto four=read(root/"independent-slices-4/nal-001.hevc");auto a=nals(coded[1]),b=nals(four);
    a.erase(std::remove_if(a.begin(),a.end(),[](const auto& nal){return nal.type>31;}),a.end());
    b.erase(std::remove_if(b.begin(),b.end(),[](const auto& nal){return nal.type>31;}),b.end());
    check(a.size()==2&&b.size()==4,"partial-picture control contains real independent entropy slices");
    std::vector<std::uint8_t> partial(coded[1].begin(),coded[1].begin()+a[0].end);partial.insert(partial.end(),four.begin()+b[2].start,four.begin()+b[2].end);
    std::vector<std::uint8_t> partialPayload;
    check(sb::wrapHevcTemporalCoverage(bytes(partial),256,256,81,2,1,false,1,partialPayload),"partial picture remains header-valid so the decoder completion guard must refuse it");
    decoder.invalidateTemporal();check(decoder.decodeTemporal(frame(0),output)==hevc::Status::Ok,"new valid key seeds strict partial-picture control");
    const auto held=output.samples;auto invalid=frame(1);invalid.payload=bytes(partialPayload);
    check(decoder.decodeTemporal(invalid,output)!=hevc::Status::Ok&&output.samples==held&&output.descriptor.sourceFrame==100,"incomplete parallel output preserves every old caller byte and identity transactionally");
    check(decoder.decodeTemporal(frame(2),output)==hevc::Status::MissingReference,"failed independent slice revokes its complete reference chain");
    check(decoder.decodeTemporal(frame(8),output)==hevc::Status::Ok&&pixelDigest(output.samples)==reference[8],"fresh key recovers exact decoded coverage after child-job failure");
    hevc::Pool pool;check(pool.prepareCold(libs),"parallel temporal pool prepared");std::array<std::vector<std::uint8_t>,32> lanes;
    for(unsigned n=0;n<32;++n){const unsigned picture=n/2;check(sb::wrapHevcTemporalCoverage(bytes(coded[picture]),256,256,81+n%2,picture+1,picture%8?picture:0,picture%8==0,(picture/8)*8+1,lanes[n]),"two independent encoder lanes wrapped");}
    unsigned sent=0,received=0;std::set<std::uint64_t> seen;hevc::Pool::Result result;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(received<32&&std::chrono::steady_clock::now()<deadline){
        while(sent<32){auto in=frame(0);in.payload=bytes(lanes[sent]);in.descriptor.sourceFrame=200+sent;if(!pool.submit(in))break;++sent;}
        while(pool.take(result)){
            check(result.status==hevc::Status::Ok&&result.decoded.descriptor.sourceFrame==result.identity.descriptor.sourceFrame,"parallel pool results retain exact current identity");
            const auto index=result.identity.descriptor.sourceFrame-200;check(index<32&&seen.insert(index).second,"each independent-slice pool result completes once");
            check(pixelDigest(result.decoded.samples)==reference[index/2],"both alternating lanes preserve every alpha byte across GOP/context transitions");++received;
        }
        std::this_thread::yield();
    }
    check(received==32&&pool.statistics().sliceBatches>=24,"bounded pool completes both non-WPP IPPP8 lanes using actual slice jobs");
    pool.cancel();auto fresh=frame(0);fresh.source.generation=99;fresh.binding[0]=91;fresh.descriptor.sourceFrame=999;
    check(pool.submit(fresh),"new source admitted after parallel cancellation");bool recovered=false;
    while(!recovered&&std::chrono::steady_clock::now()<deadline){
        if(!pool.take(result)){std::this_thread::yield();continue;}
        check(result.status==hevc::Status::Ok&&result.decoded.source.generation==99&&result.decoded.descriptor.sourceFrame==999&&pixelDigest(result.decoded.samples)==reference[0],"cancelled decoder state cannot lend pixels or identity to the new source");recovered=true;
    }
    check(recovered,"new source recovers after parallel pool cancellation");
}

}
int main(int argc,char** argv){try{if(argc!=4){std::fprintf(stderr,"Usage: test_stream_bridge_mask_hevc CODEC_LIBRARY UTILITY_LIBRARY FIXTURE_DIRECTORY\n");return 2;}const hevc::Libraries libraries{fs::absolute(argv[1]),fs::absolute(argv[2])};unavailable(libraries);decode(libraries,argv[3]);temporal(libraries,argv[3]);temporalPool(libraries,argv[3]);temporalPool(libraries,argv[3],true);reuse(libraries,argv[3]);pool(libraries,argv[3]);independentSlices(libraries,argv[3],2);independentSlices(libraries,argv[3],4);independentSlices(libraries,argv[3],2,"sao-2");independentSlices(libraries,argv[3],2,"cross-2");independentSlices(libraries,argv[3],2,"cross-sao-2");std::printf("%u real HEVC mask decoder checks passed\n",checks);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
