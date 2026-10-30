// Real Process, real signed metadata and Unix transport. Only the final GPU
// publication port and camera helper are the existing explicit fixture ports.
#include <fstream>
#define main sfnpProcessLegacyMain
#include "../stream-bridge-process/lifecycle_tests.cpp"
#undef main
#include "bridge/stream_bridge_hevc_coverage.h"
#include "pc/stream-bridge-mask-hevc-parameters.h"
namespace hevc=stream_bridge_mask_hevc;
namespace {
std::vector<std::uint8_t> codedFixture(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::uint64_t sendHevc(HandoffFixture& f,const std::vector<std::uint8_t>& payload,unsigned damage=0,unsigned width=64,unsigned height=64,bool direct=false){
    const auto frame=f.nextFrame++;
    const sb::FrameDescriptor descriptor{frame,41,std::uint16_t(width),std::uint16_t(height),1};
    const std::vector<std::uint8_t> original{0,0,0,1,0x40,1,0x91,0x80,0,0,1,0x02,1,0x80,0x42,0x80,0,0};
    std::vector<std::uint8_t> video;
    check(sb::appendCertifiedFrame({original.data(),original.size()},descriptor,payload.size()>3&&payload[3]=='T'?sb::CoverageEncoding::HevcTemporal:sb::CoverageEncoding::HevcIntra,{payload.data(),payload.size()},f.sender,video)==sb::FrameStatus::Ok,"HEVC input has actual signed original video AU and normative mask envelope");
    sb::CertifiedAccessUnit verified;check(sb::inspectCertifiedFrame({video.data(),video.size()},f.observer,verified)==sb::FrameStatus::Ok,"real complete AU authenticated before local IPC");
    local::Message message;message.kind=local::Kind::Certified;message.source.stream={f.connection,7};message.source.outputPoolGeneration=7;
    message.descriptor=descriptor;message.binding=f.binding;message.nativeTimestampToken=123;message.submittedContent=frame;message.peerConfirmed=true;
    check(sb::serializeCoverageMetadata(verified,message.certifiedMetadata),"exact signed compressed metadata detaches without granting an image");
    if(damage==1)message.certifiedMetadata[sb::FrameSignedHeaderBytes]^=1;
    if(damage==2)message.certifiedMetadata.resize(message.certifiedMetadata.size()-8);
    if(damage==3)message.certifiedMetadata.back()^=1;
    if(direct){renderer::consume(f.display,message);return frame;}
    check(f.source.channel.enqueue(message),"real authenticated local channel queues HEVC metadata");f.source.channel.tick();f.cycle();return frame;
}
void ingress(const hevc::Libraries& libraries,const std::filesystem::path& fixtures){
    HandoffFixture f(false);TransientPublication first,second;Publication legacy;
    f.display.consumers[0]={&first,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    f.display.consumers[1]={&second,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    f.display.consumers[2]={&legacy,Publication::coverage,nullptr,nullptr,Publication::admitted};
    check(f.display.hevcPool.prepareCold(libraries),"actual compositor owns an explicitly prepared decoder ABI");
    const auto coded=codedFixture(fixtures/"codes.hevc");std::vector<std::uint8_t> payload;
    check(sb::wrapHevcCoverage({coded.data(),coded.size()},64,64,payload),"CPU Main/full-range/single-IDR fixture has valid envelope");
    const auto frame=sendHevc(f,payload);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(first.decoded<1&&std::chrono::steady_clock::now()<end){f.cycle();std::this_thread::yield();}
    check(first.decoded==1&&second.decoded==1&&!first.legacy&&!second.legacy&&!legacy.certificates,"authenticated HEVC expands once into shared transient A8 and never enters legacy compressed callbacks");
    check(first.address&&first.address==second.address&&first.pixels==second.pixels&&first.pixels.size()==4096&&first.frame==frame&&first.range.minimum==0&&first.range.maximum==255,"actual Ingress retains exact source-frame receipt and full-range luma bytes");
    check(f.display.flow[unsigned(renderer::Flow::HevcDecoded)]==1&&f.display.flow[unsigned(renderer::Flow::HevcRefused)]==0,"real decode success has distinct bounded telemetry");
    sendHevc(f,payload,true);
    check(first.decoded==1&&f.display.flow[unsigned(renderer::Flow::HevcDecoded)]==1&&f.display.flow[unsigned(renderer::Flow::AuthenticationRefused)]==1,"untrusted codec selector cannot bypass authentication or dispatch samples");
    sendHevc(f,payload,2);sendHevc(f,payload,3);
    check(first.decoded==1&&f.display.flow[unsigned(renderer::Flow::HevcDecoded)]==1&&f.display.flow[unsigned(renderer::Flow::AuthenticationRefused)]==3,"network truncation and changed CABAC bytes fail complete signed-payload validation before decode");
    // Decoder flags are not a complete CABAC validator: some library versions
    // accept a prefix without marking corruption. The transport must reject
    // truncation against the original signed byte count/hash, as checked above.
    f.display.cameraRecovery=false;f.display.cameraPeer=false;
    f.display.hevcPool.stopCold();const auto missing=sendHevc(f,payload);
    check(first.decoded==1&&f.display.flow[unsigned(renderer::Flow::HevcRefused)]==1&&f.display.masks[missing%f.display.masks.size()].descriptor.sourceFrame!=missing,"missing optional backend does not publish a mask or invent decoder state");
    check(f.display.cameraPeer&&f.display.cameraRecovery.load(),"authenticated active MR policy retains camera-only recovery after decoder refusal");
    f.queue(true,0,false);f.cycle();
    check(first.decoded==2&&second.decoded==2&&legacy.certificates==1&&first.pixels==std::vector<std::uint8_t>(32,127),"unavailable HEVC backend preserves existing lossless single-expansion and legacy dispatch");
}
void multisliceIngress(const hevc::Libraries& libraries,const std::filesystem::path& fixtures){
    HandoffFixture f(false);TransientPublication first,second;
    f.display.consumers[0]={&first,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    f.display.consumers[1]={&second,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    check(f.display.hevcPool.prepareCold(libraries),"multi-slice ingress prepares actual decoder");
    const auto coded=codedFixture(fixtures/"slices.hevc");std::vector<std::uint8_t> payload;
    check(sb::wrapHevcCoverage({coded.data(),coded.size()},256,256,payload),"real two-slice IDR and unused SPS reference set enter signed transport");
    const auto frame=sendHevc(f,payload,0,256,256);
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(first.decoded<1&&std::chrono::steady_clock::now()<end){f.cycle();std::this_thread::yield();}
    check(first.decoded==1&&second.decoded==1&&first.frame==frame&&second.frame==frame&&first.address==second.address&&first.pixels.size()==65536,"both consumers share exactly one full decoded mask for the authenticated frame");
    unsigned error=0;for(unsigned i=0;i<first.pixels.size();++i)error=std::max(error,unsigned(std::abs(int(first.pixels[i])-int(i%256))));
    check(error<=2&&first.pixels==second.pixels,"both real entropy-coded slices contribute the expected full-range luma");
    sendHevc(f,payload,3,256,256);
    check(first.decoded==1&&f.display.flow[unsigned(renderer::Flow::AuthenticationRefused)]==1,"changed second-slice entropy cannot publish another authenticated mask");
}

void backpressureAndRevocation(const hevc::Libraries& libraries,const std::filesystem::path& fixtures){
    const auto coded=codedFixture(fixtures/"codes.hevc");std::vector<std::uint8_t> payload;
    check(sb::wrapHevcCoverage({coded.data(),coded.size()},64,64,payload),"backpressure fixture wrapped");
    {HandoffFixture f(false);TransientPublication publication;
     f.display.consumers[0]={&publication,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
     check(f.display.hevcPool.prepareCold(libraries),"real backpressure pool prepared");
     // Complete codec jobs without consuming their slots. They have no current
     // authenticated Process receipt and must never become renderer authority.
     stream_bridge_mask_hevc::Input preload;preload.source=f.display.verifiedSource;preload.binding=f.binding;preload.descriptor={1,41,64,64,1};preload.payload={payload.data(),payload.size()};
     for(unsigned i=0;i<stream_bridge_mask_hevc::Pool::Capacity;++i){preload.descriptor.sourceFrame=i+1;check(f.display.hevcPool.submit(preload),"fixed pool filled independently of Process publication");}
     const auto frame=sendHevc(f,payload,0,64,64,true);const auto index=frame%f.display.pendingHevc.size();
     check(f.display.pendingHevc[index].phase==renderer::Process::PendingHevc::Phase::Waiting&&f.display.flow[unsigned(renderer::Flow::HevcBackpressure)]==1&&!publication.decoded,"full decoder pool retains the exact authenticated mask instead of blocking IPC or dropping it");
     const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
     while((publication.decoded<1||f.display.flow[unsigned(renderer::Flow::HevcStale)]<stream_bridge_mask_hevc::Pool::Capacity)&&std::chrono::steady_clock::now()<end){f.cycle();std::this_thread::yield();}
     check(publication.decoded==1&&publication.frame==frame&&f.display.pendingHevc[index].phase==renderer::Process::PendingHevc::Phase::Empty,"same pending frame retries after capacity is freed and publishes exactly once");
     check(f.display.flow[unsigned(renderer::Flow::HevcStale)]==stream_bridge_mask_hevc::Pool::Capacity,"codec completion without a matching current authenticated receipt cannot publish pixels");}
    {HandoffFixture f(false);TransientPublication publication;
     f.display.consumers[0]={&publication,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
     check(f.display.hevcPool.prepareCold(libraries),"revocation pool prepared");sendHevc(f,payload,0,64,64,true);
     f.display.channel.stop();f.cycle();
     for(unsigned i=0;i<32;++i){f.cycle();std::this_thread::yield();}
     check(!publication.decoded&&!f.display.cameraPeer&&std::all_of(f.display.pendingHevc.begin(),f.display.pendingHevc.end(),[](const auto& item){return item.phase==renderer::Process::PendingHevc::Phase::Empty;}),"channel revocation cancels pending/completed decode without stale pixel or camera authority");}
}
struct ExactHevcPublication {
    std::uint64_t frame=0;bool gpuReady=false;unsigned decoded=0;
    static bool admitted(void*)noexcept{return true;}
    static void samples(void* pointer,const sb::CertifiedAccessUnit& certificate,sb::Bytes pixels,sb::CoverageRange)noexcept{
        auto& p=*static_cast<ExactHevcPublication*>(pointer);if(pixels.size!=4096)return;p.frame=certificate.descriptor.sourceFrame;++p.decoded;
    }
    static bool published(void* pointer,const renderer::LocalPicture& picture)noexcept{
        const auto& p=*static_cast<ExactHevcPublication*>(pointer);return p.gpuReady&&p.frame==picture.metadata.descriptor.sourceFrame;
    }
};
void hevcNativeHandoff(const hevc::Libraries& libraries,const std::filesystem::path& fixtures,bool temporal=false){
    HandoffFixture f;ExactHevcPublication publication;
    f.display.consumers[0]={&publication,nullptr,nullptr,nullptr,ExactHevcPublication::admitted,ExactHevcPublication::published,ExactHevcPublication::samples};
    check(f.display.hevcPool.prepareCold(libraries),"handoff decoder workers prepared before native DQ");
    std::vector<std::uint8_t> payload;
    if(temporal){
        sb::sender::MaskHevcParameters parameters;parameters.reset(91);
        for(unsigned n=0;n<8;++n){char name[40];std::snprintf(name,sizeof name,"temporal-%03u.hevc",n);const auto original=codedFixture(fixtures/name);auto coded=original;
            if(n){
                for(std::size_t at=0;at+5<coded.size();++at)if(!coded[at]&&!coded[at+1]&&(coded[at+2]==1||(coded[at+2]==0&&coded[at+3]==1))){
                    const auto body=at+(coded[at+2]==1?3:4);if(((coded[body]>>1)&63)==1){coded.erase(coded.begin(),coded.begin()+at);break;}at=body+1;}
            }
            check(parameters.complete(coded,{91,n+1,n?n:0,1,n==0})==(n?sb::sender::MaskHevcParameters::Result::Restored:sb::sender::MaskHevcParameters::Result::KeyCached)&&coded==original,"real P-image handoff starts from omitted-header driver output and exact group repair");
            check(sb::wrapHevcTemporalCoverage({coded.data(),coded.size()},64,64,91,n+1,n?n:0,n==0,1,payload),"real temporal frame wrapped for signed native image join");
            if(n<7){sendHevc(f,payload);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(publication.decoded<n+1&&std::chrono::steady_clock::now()<end){f.cycle();std::this_thread::yield();}
                check(publication.decoded==n+1,"authenticated predecessors establish decoder references without granting an image");}
        }
    }else{const auto coded=codedFixture(fixtures/"codes.hevc");check(sb::wrapHevcCoverage({coded.data(),coded.size()},64,64,payload),"actual independent HEVC mask wrapped for native image join");}
    const unsigned before=publication.decoded;
    const auto frame=f.nextFrame++;const sb::FrameDescriptor descriptor{frame,41,64,64,1};
    const std::vector<std::uint8_t> original{0,0,0,1,0x40,1,0x91,0x80,0,0,1,0x02,1,0x80,0x42,0x80,0,0};std::vector<std::uint8_t> video;
    check(sb::appendCertifiedFrame({original.data(),original.size()},descriptor,payload.size()>3&&payload[3]=='T'?sb::CoverageEncoding::HevcTemporal:sb::CoverageEncoding::HevcIntra,{payload.data(),payload.size()},f.sender,video)==sb::FrameStatus::Ok,"actual source video and HEVC payload jointly signed");
    sb::CertifiedAccessUnit verified;check(sb::inspectCertifiedFrame({video.data(),video.size()},f.observer,verified)==sb::FrameStatus::Ok,"native handoff input has verified complete original video AU");
    native::CaptureObservation capture{native::CaptureKind::Decoded,23,f.file.fd,2,71,7,21,frame};f.source.writer.captureEvent(capture);local::LeaseReference lease;
    check(f.source.writer.reference(capture,lease),"exact shared native capture lease recorded");
    local::Message certificate;certificate.kind=local::Kind::Certified;certificate.source.stream=f.display.verifiedSource;certificate.source.outputPoolGeneration=7;
    certificate.descriptor=descriptor;certificate.binding=f.binding;certificate.nativeTimestampToken=capture.nativeTimestampToken;certificate.submittedContent=frame;certificate.peerConfirmed=true;
    check(sb::serializeCoverageMetadata(verified,certificate.certifiedMetadata),"native handoff metadata retains exact signed compressed mask");
    auto decoded=certificate;decoded.kind=local::Kind::Decoded;decoded.certifiedMetadata.clear();decoded.lease=lease;decoded.dmabuf=f.file.fd;decoded.readOnlyLeases=f.source.writer.duplicateReadOnly();
    check(decoded.readOnlyLeases>=0&&f.source.channel.enqueue(certificate)&&f.source.channel.enqueue(decoded),"actual signed certificate and decoded-image ports queued together");close(decoded.readOnlyLeases);decoded.dmabuf=decoded.readOnlyLeases=-1;
    f.source.awaiting[lease.slot]=decoded;f.source.channel.tick();auto dq=f.start(decoded);f.waiting();
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(90);
    while(publication.decoded==before&&std::chrono::steady_clock::now()<end){f.cycle();std::this_thread::yield();}
    check(publication.decoded==before+1&&publication.frame==frame&&!f.display.flow[unsigned(renderer::Flow::ReadyQueued)]&&dq.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,"decoded mask and authenticated image still cannot release native DQ before exact GPU publication");
    publication.gpuReady=true;++publication.frame;f.cycle();
    check(!f.display.flow[unsigned(renderer::Flow::ReadyQueued)],"another frame's published mask cannot acknowledge this native image");
    publication.frame=frame;f.cycle();check(dq.wait_for(std::chrono::seconds(1))==std::future_status::ready,"exact decoded mask/image/GPU join returns through real Ready IPC");dq.get();
    check(f.source.flow[unsigned(renderer::Flow::HandoffReady)]==1&&!f.source.flow[unsigned(renderer::Flow::HandoffTimeout)],"asynchronous HEVC preserves the original exact native publication acknowledgement");
}

}
int main(int argc,char** argv){try{if(argc==2&&!std::strcmp(argv[1],"--process-lifecycle"))return sfnpProcessLegacyMain(argc,argv);if(argc!=4)return 2;ingress({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3]);multisliceIngress({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3]);backpressureAndRevocation({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3]);hevcNativeHandoff({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3]);hevcNativeHandoff({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3],true);std::printf("%u real authenticated HEVC Ingress checks passed\n",checks);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
