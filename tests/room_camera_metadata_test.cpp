#include "room_scan/camera_api/capture_metadata.h"
#include <cstdio>
using namespace room_camera_api;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void put(std::vector<std::uint8_t>& data,std::size_t offset,std::uint64_t value){
    for(unsigned i=0;i<8;++i)data.at(offset+i)=std::uint8_t(value>>(8*i));
}
void record(std::vector<std::uint8_t>& data,std::size_t offset,std::uint64_t time){
    put(data,offset,time-10000000);put(data,offset+8,time-1000000);put(data,offset+16,time-9900000);
    put(data,offset+24,time+789);put(data,offset+32,std::uint64_t(std::int64_t(-123456)));
    data.at(offset+40)=0x5a;data.at(offset+41)=0xa5;
}
}
int main(){try{
    constexpr std::uint64_t event=2000000000000ULL;std::vector<std::uint8_t> data(4096,0);
    record(data,256,event);const auto value=captureMetadata(data.data(),data.size(),event);
    check(value&&value->offset==256&&value->sofRaw==event-10000000&&value->eofRaw==event-1000000,"exact record and native clock fields");
    check(value->eofInterruptRaw==event+789&&value->ispToRaw==-123456,"V4L2 microsecond serialization and signed clock offset");
    check(sensorMetadataHex(*value).rfind("5aa5",0)==0&&sensorMetadataHex(*value).size()==2416,"bounded embedded sensor payload");
    check(!captureMetadata(data.data(),data.size(),event+1000),"another frame cannot borrow the last metadata");
    check(!captureMetadata(data.data(),100,event),"short metadata is refused");
    put(data,256,event+1000000);check(!captureMetadata(data.data(),data.size(),event),"future SOF cannot attest a frame");
    record(data,256,event);record(data,2048,event);bool ambiguous=false;
    try{captureMetadata(data.data(),data.size(),event);}catch(const std::runtime_error&){ambiguous=true;}
    check(ambiguous,"duplicate matching metadata is refused rather than selected heuristically");
    std::puts("PASS CAMSS metadata identity, quantization, clock fields and ambiguity rejection");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
