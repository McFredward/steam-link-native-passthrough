// Real Process, real signed metadata and Unix transport. Only the final GPU
// publication port and camera helper are the existing explicit fixture ports.
#include <fstream>
#define main sfnpProcessLegacyMain
#include "../stream-bridge-process/lifecycle_tests.cpp"
#undef main
#include "bridge/stream_bridge_hevc_coverage.h"
namespace hevc=stream_bridge_mask_hevc;
namespace {
std::vector<std::uint8_t> codedFixture(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
std::uint64_t sendHevc(HandoffFixture& f,const std::vector<std::uint8_t>& payload,unsigned damage=0){
    const auto frame=f.nextFrame++;
    const sb::FrameDescriptor descriptor{frame,41,64,64,1};
    const std::vector<std::uint8_t> original{0,0,0,1,0x40,1,0x91,0x80,0,0,1,0x02,1,0x80,0x42,0x80,0,0};
    std::vector<std::uint8_t> video;
    check(sb::appendCertifiedFrame({original.data(),original.size()},descriptor,sb::CoverageEncoding::HevcIntra,{payload.data(),payload.size()},f.sender,video)==sb::FrameStatus::Ok,"HEVC input has actual signed original video AU and normative mask envelope");
    sb::CertifiedAccessUnit verified;check(sb::inspectCertifiedFrame({video.data(),video.size()},f.observer,verified)==sb::FrameStatus::Ok,"real complete AU authenticated before local IPC");
    local::Message message;message.kind=local::Kind::Certified;message.source.stream={f.connection,7};message.source.outputPoolGeneration=7;
    message.descriptor=descriptor;message.binding=f.binding;message.nativeTimestampToken=123;message.submittedContent=frame;message.peerConfirmed=true;
    check(sb::serializeCoverageMetadata(verified,message.certifiedMetadata),"exact signed compressed metadata detaches without granting an image");
    if(damage==1)message.certifiedMetadata[sb::FrameSignedHeaderBytes]^=1;
    if(damage==2)message.certifiedMetadata.resize(message.certifiedMetadata.size()-8);
    if(damage==3)message.certifiedMetadata.back()^=1;
    check(f.source.channel.enqueue(message),"real authenticated local channel queues HEVC metadata");f.source.channel.tick();f.cycle();return frame;
}
void ingress(const hevc::Libraries& libraries,const std::filesystem::path& fixtures){
    HandoffFixture f(false);TransientPublication first,second;Publication legacy;
    f.display.consumers[0]={&first,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    f.display.consumers[1]={&second,TransientPublication::legacyCallback,nullptr,nullptr,TransientPublication::admitted,nullptr,TransientPublication::decodedCallback};
    f.display.consumers[2]={&legacy,Publication::coverage,nullptr,nullptr,Publication::admitted};
    check(f.display.maskHevc.prepareCold(libraries),"actual compositor owns an explicitly prepared decoder ABI");
    const auto coded=codedFixture(fixtures/"codes.hevc");std::vector<std::uint8_t> payload;
    check(sb::wrapHevcCoverage({coded.data(),coded.size()},64,64,payload),"CPU Main/full-range/single-IDR fixture has valid envelope");
    const auto frame=sendHevc(f,payload);
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
    f.display.maskHevc.stopCold();const auto missing=sendHevc(f,payload);
    check(first.decoded==1&&f.display.flow[unsigned(renderer::Flow::HevcRefused)]==1&&f.display.masks[missing%f.display.masks.size()].descriptor.sourceFrame!=missing,"missing optional backend does not publish a mask or invent decoder state");
    check(f.display.cameraPeer&&f.display.cameraRecovery.load(),"authenticated active MR policy retains camera-only recovery after decoder refusal");
    f.queue(true,0,false);f.cycle();
    check(first.decoded==2&&second.decoded==2&&legacy.certificates==1&&first.pixels==std::vector<std::uint8_t>(32,127),"unavailable HEVC backend preserves existing lossless single-expansion and legacy dispatch");
}
}
int main(int argc,char** argv){try{if(argc!=4)return 2;ingress({std::filesystem::absolute(argv[1]),std::filesystem::absolute(argv[2])},argv[3]);std::printf("%u real authenticated HEVC Ingress checks passed\n",checks);return 0;}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
