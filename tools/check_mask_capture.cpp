// Offline diagnostic decoding only. Never imports video, pairing or camera state.
#include "pc/stream-bridge-mask-capture.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace cap=stream_bridge::sender::capture;
using namespace stream_bridge;
namespace fs=std::filesystem;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void read(std::ifstream& stream,void* data,std::size_t size){stream.read(static_cast<char*>(data),static_cast<std::streamsize>(size));require(bool(stream),"truncated capture");}
bool sameSource(const cap::Metadata& a,const cap::Metadata& b){return a.pid==b.pid&&a.encoderGeneration==b.encoderGeneration&&a.sourceContract==b.sourceContract&&a.width==b.width&&a.height==b.height;}
}
int main(int argc,char** argv){try{
    if(argc!=2&&argc!=5){std::cerr<<"Usage: check_mask_capture CAPTURE [--frame INDEX RAW_OUTPUT]\n";return 2;}
    unsigned selected=UINT32_MAX;fs::path target;
    if(argc==5){require(std::string(argv[2])=="--frame","unknown option");const auto number=std::stoul(argv[3]);require(number<cap::MaximumFrames,"frame index outside capture bounds");selected=static_cast<unsigned>(number);target=fs::u8path(argv[4]);require(!fs::exists(fs::symlink_status(target)),"raw output already exists");}
    const auto path=fs::u8path(argv[1]);require(fs::file_size(path)<=cap::MaximumBytes,"capture too large");std::ifstream stream(path,std::ios::binary);require(bool(stream),"cannot open capture");
    std::array<std::uint8_t,cap::HeaderBytes> header{};read(stream,header.data(),header.size());cap::Token token{};require(cap::parseHeader({header.data(),header.size()},token),"invalid capture header");
    std::uint64_t used=cap::HeaderBytes,lastFrame=0;std::uint32_t count=0;cap::Metadata previous{};std::vector<std::uint8_t> saved;
    for(;;){
        std::array<std::uint8_t,cap::RecordBytes> bytes{};read(stream,bytes.data(),8);
        if(!std::memcmp(bytes.data(),"SFNPDONE",8)){
            read(stream,bytes.data()+8,cap::FooterBytes-8);cap::Stop reason{};std::uint32_t frames=0;std::uint64_t total=0,last=0;
            require(cap::parseFooter({bytes.data(),cap::FooterBytes},reason,frames,total,last)&&frames==count&&total==used+cap::FooterBytes&&last==lastFrame,"capture footer mismatch");
            require(stream.peek()==std::char_traits<char>::eof(),"trailing capture bytes");
            if(selected!=UINT32_MAX){require(!saved.empty(),"selected frame absent");std::ofstream output(target,std::ios::binary);output.write(reinterpret_cast<const char*>(saved.data()),saved.size());output.close();require(bool(output),"raw output write failed");}
            std::cout<<"{\"frames\":"<<frames<<",\"bytes\":"<<total<<",\"reason\":\""<<cap::stopName(reason)<<"\",\"all_decoded_exact_extent\":true}\n";return 0;
        }
        require(count<cap::MaximumFrames,"too many frames");read(stream,bytes.data()+8,bytes.size()-8);cap::Metadata metadata{};std::uint32_t size=0;std::array<std::uint8_t,32> digest{};
        require(cap::parseRecordHeader({bytes.data(),bytes.size()},metadata,size,digest),"invalid record header");
        require(used<=cap::MaximumBytes-cap::RecordBytes-cap::FooterBytes&&size<=cap::MaximumBytes-used-cap::RecordBytes-cap::FooterBytes,"capture byte limit exceeded");
        if(count)require(sameSource(previous,metadata)&&lastFrame!=UINT64_MAX&&metadata.frame==lastFrame+1&&metadata.timestampUs>=previous.timestampUs,"discontinuous actual source/frame sequence");
        CertifiedAccessUnit sample;sample.descriptor={metadata.frame,metadata.encoderGeneration,metadata.width,metadata.height};sample.coverageEncoding=metadata.encoding;sample.coverage.resize(size);read(stream,sample.coverage.data(),sample.coverage.size());
        require(cap::verifyRecord({bytes.data(),bytes.size()},{sample.coverage.data(),sample.coverage.size()}),"record checksum mismatch");std::vector<std::uint8_t> raw;require(decodeCertifiedCoverage(sample,raw)&&raw.size()==std::size_t(metadata.width)*metadata.height,"invalid compressed mask");
        std::cout<<"{\"index\":"<<count<<",\"frame\":"<<metadata.frame<<",\"width\":"<<metadata.width<<",\"height\":"<<metadata.height<<",\"encoding\":"<<unsigned(metadata.encoding)<<",\"payload_bytes\":"<<size<<",\"raw_bytes\":"<<raw.size()<<",\"qos_revision\":"<<metadata.qosRevision<<",\"qos_flags\":"<<metadata.qosFlags<<"}\n";
        if(count==selected)saved.swap(raw);
        previous=metadata;lastFrame=metadata.frame;++count;used+=cap::RecordBytes+size;
    }
}catch(const std::exception& error){std::cerr<<"Capture check failed: "<<error.what()<<'\n';return 1;}}
