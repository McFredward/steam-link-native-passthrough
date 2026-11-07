#include "room_scan/camera_api/capture_metadata.h"
#include "room_scan/camera_api/metadata_observations.h"
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
Json diagnostic(unsigned sequence){
    return {{"schema","sfnp-native-camera-metadata-v1"},{"source_session","fixture-metadata-session"},
        {"producer_pid",123},{"producer_start_ticks","456"},{"sequence",sequence},
        {"contains_image_data",false},{"room_scan_ready",false}};
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
    MetadataObservations history;
    for(unsigned i=1;i<=35;++i)history.add(diagnostic(i),1000000000u+i*1000);
    check(history.next(0,1000040000u).at("status")=="metadata-history-evicted"
        &&history.next(0,1000040000u).at("resume_after")==3,"slow metadata consumer is told exactly what was evicted");
    auto next=history.next(3,1000040000u);
    check(next.at("observation_token")==4&&next.at("metadata").at("sequence")==4
        &&!next.at("contains_image_data").get<bool>()&&!next.at("room_scan_ready").get<bool>(),"metadata cursor names its exact retained event without scan authority");
    check(history.next(4,1000040000u).at("observation_token")==5,"reading does not consume another client's diagnostic event");
    check(history.next(36,1000040000u).at("status")=="invalid-metadata-cursor","a different session's future cursor is invalid");
    check(history.next(35,1000040000u).at("status")=="no-fresh-metadata","no newer event cannot reuse the last record");
    check(history.next(3,1000004000u+500000000u).at("status")=="no-fresh-metadata","expired diagnostic records cannot be served as current");
    check(history.next(3,999999999u).at("status")=="no-fresh-metadata","future metadata cannot be served as current");
    for(unsigned failure=0;failure<5;++failure){
        auto invalid=diagnostic(36);std::uint64_t stamp=1000050000u;
        if(failure==0)invalid["images"]={"not-a-metadata-field"};
        if(failure==1)invalid["source_session"]="another-producer-session";
        if(failure==2)invalid["padding"]=std::string(12289,'x');
        if(failure==3)stamp=999999999u;
        if(failure==4)invalid["room_scan_ready"]=true;
        bool refused=false;try{history.add(std::move(invalid),stamp);}catch(const std::exception&){refused=true;}
        check(refused,"invalid metadata is refused without consuming a token");
    }
    history.add(diagnostic(36),1000050000u);check(history.next(35,1000050000u).at("observation_token")==36,"refused writes never alter retained cursor identity");
    std::puts("PASS CAMSS metadata identity, quantization, clock fields and ambiguity rejection");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
