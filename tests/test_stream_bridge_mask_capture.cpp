#include "pc/stream-bridge-mask-capture.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
using namespace stream_bridge;
namespace cap=stream_bridge::sender::capture;
namespace fs=std::filesystem;
namespace {
unsigned checks=0;
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
Bytes bytes(const std::vector<std::uint8_t>& data){return {data.data(),data.size()};}
cap::Metadata metadata(std::uint64_t frame=1){cap::Metadata m;m.frame=frame;m.encoderGeneration=7;m.sourceContract=19;m.timestampUs=1000+frame;m.pid=42;m.width=16;m.height=16;m.encoding=CoverageEncoding::Raw;m.originalVideoBytes=100;m.completeAuBytes=500;return m;}
std::vector<std::uint8_t> pixels(){std::vector<std::uint8_t> out(256);for(unsigned i=0;i<out.size();++i)out[i]=std::uint8_t(i);return out;}
std::vector<std::uint8_t> read(const fs::path& file){std::ifstream stream(file,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
struct Directory {fs::path path=fs::temp_directory_path()/("sfnp-mask-capture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Directory(){fs::create_directory(path);}~Directory(){std::error_code error;fs::remove_all(path,error);}};
void request(const fs::path& root,const char* token="0123456789abcdef0123456789abcdef"){std::ofstream stream(root/"mask-capture.request");stream<<"SFNP_MASK_CAPTURE_V1\n"<<token<<'\n';}
void format(){
    cap::Token token{};token[2]=7;const auto header=cap::header(token);cap::Token parsed{};check(cap::parseHeader({header.data(),header.size()},parsed)&&parsed==token,"header preserves request identity");
    auto changed=header;changed[40]=1;check(!cap::parseHeader({changed.data(),changed.size()},parsed),"reserved header bytes rejected");
    cap::Sample sample{metadata(),pixels()};const auto original=sample.payload;auto h=cap::recordHeader(sample);cap::Metadata actual;std::uint32_t length=0;std::array<std::uint8_t,32> digest{};
    check(cap::parseRecordHeader({h.data(),h.size()},actual,length,digest)&&length==256&&actual.frame==1&&actual.sourceContract==19,"record has actual frame and source epoch");
    check(cap::verifyRecord({h.data(),h.size()},bytes(sample.payload)),"all256 alpha bytes retain digest");sample.payload[100]^=1;check(!cap::verifyRecord({h.data(),h.size()},bytes(sample.payload)),"corrupt captured bytes rejected");sample.payload=original;
    auto modified=h;modified[88]^=1;check(!cap::verifyRecord({modified.data(),modified.size()},bytes(sample.payload)),"corrupt QoS observations are detected too");
    for(unsigned offset:{0u,4u,8u,12u,16u,24u,32u,48u,52u,54u,56u,60u,136u}){auto bad=h;if(offset==136)bad[offset]=1;else for(unsigned i=0;i<(offset==52||offset==54?2:4);++i)bad[offset+i]=0;check(!cap::parseRecordHeader({bad.data(),bad.size()},actual,length,digest),"invalid and zero record fields rejected");}
    sample.metadata.qosRevision=13;sample.metadata.qosFlags=7;sample.metadata.frameRateNum=90;sample.metadata.frameRateDen=1;sample.metadata.requestedAverageBitRate=100000000;sample.metadata.appliedAverageBitRate=1000000;sample.metadata.reservedMetadataBitRate=200000000;
    h=cap::recordHeader(sample);check(cap::parseRecordHeader({h.data(),h.size()},actual,length,digest)&&actual.qosRevision==13&&actual.qosFlags==7&&actual.frameRateNum==90&&actual.requestedAverageBitRate==100000000&&actual.appliedAverageBitRate==1000000&&actual.reservedMetadataBitRate==200000000,"per-submission observed QoS stays explicit and byte exact");
    auto end=cap::footer(cap::Stop::FrameGap,1,cap::HeaderBytes+cap::RecordBytes+256+cap::FooterBytes,1);cap::Stop reason{};std::uint32_t frames=0;std::uint64_t total=0,last=0;
    check(cap::parseFooter({end.data(),end.size()},reason,frames,total,last)&&reason==cap::Stop::FrameGap&&frames==1&&last==1,"partial captures state their real stop reason");
    CertifiedAccessUnit frame;frame.descriptor={1,7,16,16};frame.coverageEncoding=CoverageEncoding::Raw;frame.coverage=original;std::vector<std::uint8_t> decoded;
    check(decodeCertifiedCoverage(frame,decoded)&&decoded==original,"captured codec maps to existing byte-exact offline decoder only");
}
void queue(){
    cap::Queue queue;auto data=pixels();queue.offer(metadata(),std::move(data));check(data.size()==256&&!queue.active(),"off by default does not consume producer data");queue.start();
    for(unsigned i=1;i<=cap::MaximumFrames;++i){auto payload=pixels();queue.offer(metadata(i),std::move(payload));cap::Sample got;check(queue.pop(got)&&got.metadata.frame==i&&got.payload==pixels(),"bounded FIFO preserves real submission and every alpha value");}
    check(queue.reason()==cap::Stop::Complete,"maximum frame count finishes capture");
    queue.start();for(unsigned i=1;i<=4;++i){auto payload=pixels();queue.offer(metadata(i),std::move(payload));}auto overflow=pixels();queue.offer(metadata(5),std::move(overflow));check(queue.reason()==cap::Stop::QueueFull&&overflow.size()==256,"diagnostic overflow retains current game vector");
    queue.start();auto first=pixels();queue.offer(metadata(),std::move(first));auto gap=pixels();queue.offer(metadata(3),std::move(gap));check(queue.reason()==cap::Stop::FrameGap&&gap.size()==256,"dropped actual submission cannot become consecutive data");
    queue.start();first=pixels();queue.offer(metadata(),std::move(first));auto changed=metadata(2);changed.sourceContract=20;auto other=pixels();queue.offer(changed,std::move(other));check(queue.reason()==cap::Stop::SourceChanged&&other.size()==256,"source epochs never mix");
    queue.start();auto large=metadata();large.width=4096;large.height=2048;std::vector<std::uint8_t> payload(cap::MaximumPayload,17);
    for(unsigned i=1;i<=6;++i){large.frame=i;large.timestampUs=1000+i;auto copy=payload;queue.offer(large,std::move(copy));cap::Sample out;if(i<6)check(queue.pop(out),"bounded large record accepted before disk limit");}
    check(queue.reason()==cap::Stop::ByteLimit,"disk limit includes headers and footer");
}
void writer(){
    Directory dir;cap::Writer writer;writer.configure(dir.path);check(!fs::exists(dir.path/"mask-capture"),"configuration alone creates no captures");request(dir.path);writer.poll();
    check(fs::exists(dir.path/"mask-capture.request"),"explicit request remains a cancellable diagnostic lease");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));auto payload=pixels();writer.offer(metadata(),std::move(payload));writer.stop();
    check(!fs::exists(dir.path/"mask-capture.request"),"completed request consumed once");
    const auto raw=read(dir.path/"mask-capture/latest.sfmc");check(raw.size()>=cap::HeaderBytes+cap::FooterBytes&&raw.size()<=cap::MaximumBytes,"bounded diagnostic file produced");
    cap::Token token{};check(cap::parseHeader({raw.data(),cap::HeaderBytes},token),"writer emits actual capture header");cap::Stop reason{};std::uint32_t frames=0;std::uint64_t total=0,last=0;
    check(cap::parseFooter({raw.data()+raw.size()-cap::FooterBytes,cap::FooterBytes},reason,frames,total,last)&&total==raw.size()&&frames==1&&last==1,"writer finalizes accepted data and byte count");
    check(reason==cap::Stop::Stopped,"explicit stop is marked partial rather than complete");
    const auto firstSize=raw.size();request(dir.path,"1123456789abcdef0123456789abcdef");
    std::this_thread::sleep_for(std::chrono::milliseconds(1020));writer.poll();
    check(writer.active(),"new explicit nonce can reuse finished writer");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));auto next=pixels();writer.offer(metadata(51),std::move(next));
    fs::remove(dir.path/"mask-capture.request");std::this_thread::sleep_for(std::chrono::milliseconds(1020));writer.poll();writer.stop();
    const auto reused=read(dir.path/"mask-capture/latest.sfmc");
    check(reused.size()==firstSize&&cap::parseFooter({reused.data()+reused.size()-cap::FooterBytes,cap::FooterBytes},reason,frames,total,last)&&frames==1&&last==51&&reason==cap::Stop::Stopped,"cancellation is partial and next request replaces one fixed bounded capture");
    unsigned captures=0;for(const auto& entry:fs::directory_iterator(dir.path/"mask-capture"))if(entry.path().extension()==".sfmc")++captures;
    check(captures==1,"captures never accumulate versioned data");
    Directory unowned;fs::create_directory(unowned.path/"mask-capture");std::ofstream(unowned.path/"mask-capture/notes.txt")<<"personal";cap::Writer refuse;refuse.configure(unowned.path);request(unowned.path);refuse.poll();check(fs::exists(unowned.path/"mask-capture.request")&&!fs::exists(unowned.path/"mask-capture/latest.sfmc"),"unowned directory not overwritten");
#ifndef _WIN32
    Directory links;Directory outside;fs::create_directory_symlink(outside.path,links.path/"mask-capture");cap::Writer linked;linked.configure(links.path);request(links.path);linked.poll();check(fs::is_empty(outside.path),"linked destination never followed");
#endif
}
void hardlinks(){
    for(const char* filename:{"owner.txt","status.json","latest.sfmc","mask-capture.request"}){
        Directory dir;fs::create_directory(dir.path/"mask-capture");
        if(std::string(filename)!="owner.txt")std::ofstream(dir.path/"mask-capture/owner.txt")<<"SFNP private mask capture v1\n";
        const auto outside=dir.path/"personal.txt";std::string value="SFNPMASKpersonal";
        if(std::string(filename)=="owner.txt")value="SFNP private mask capture v1\n";
        if(std::string(filename)=="status.json")value="{\"schema\":\"sfnp-mask-capture-status-v1\"}\n";
        if(std::string(filename)=="mask-capture.request")value="SFNP_MASK_CAPTURE_V1\n0123456789abcdef0123456789abcdef\n";
        std::ofstream(outside)<<value;
        const auto target=std::string(filename)=="mask-capture.request"?dir.path/filename:dir.path/"mask-capture"/filename;
        fs::create_hard_link(outside,target);if(std::string(filename)!="mask-capture.request")request(dir.path);
        cap::Writer writer;writer.configure(dir.path);writer.poll();writer.stop();
        const auto after=read(outside);check(!writer.active()&&std::string(after.begin(),after.end())==value,"hard-linked request, owner and output targets refused without changing foreign data");
    }
}
void fixture(const fs::path& root){
    check(fs::is_directory(root)&&!fs::exists(root/"mask-capture"),"fixture root must be fresh and private");
    cap::Writer writer;writer.configure(root);request(root);writer.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    for(unsigned i=0;i<2;++i){auto m=metadata(41+i);m.qosRevision=9;m.qosFlags=7;m.frameRateNum=90;m.frameRateDen=1;
        m.requestedAverageBitRate=200000000;m.appliedAverageBitRate=1000000;m.reservedMetadataBitRate=199000000;m.nativeInputTimestamp=123000+i;
        auto data=pixels();if(i)data[0]=255;writer.offer(m,std::move(data));std::this_thread::sleep_for(std::chrono::milliseconds(25));}
    writer.stop();
}
}
int main(int argc,char** argv){try{if(argc==3&&std::string(argv[1])=="--fixture"){fixture(fs::u8path(argv[2]));return 0;}check(argc==1,"unknown test argument");format();queue();writer();hardlinks();std::printf("%u bounded private capture checks passed\n",checks);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
