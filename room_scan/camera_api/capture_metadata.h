// Decode the existing CAMSS metadata ABI without configuring any output.
// Discovery uses the exact V4L2 event time (serialized at microsecond precision),
// queue binding and a unique complete record; ambiguous records are refused.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

namespace room_camera_api {
struct CaptureMetadata {
    std::size_t offset=0;
    std::uint64_t sofRaw=0,eofRaw=0,sofInterruptRaw=0,eofInterruptRaw=0;
    std::int64_t ispToRaw=0;
    std::array<std::uint8_t,1208> sensor{};
};
inline std::uint64_t metadataWord(const std::uint8_t* data){
    std::uint64_t result=0;for(unsigned i=0;i<8;++i)result|=std::uint64_t(data[i])<<(i*8);return result;
}
inline std::optional<CaptureMetadata> captureMetadata(const std::uint8_t* data,std::size_t bytes,std::uint64_t v4lTime){
    constexpr std::size_t extent=40+1208;
    if(!data||bytes<extent||bytes>2*1024*1024||!v4lTime||v4lTime%1000)return std::nullopt;
    std::optional<CaptureMetadata> found;
    for(std::size_t offset=0;offset<=bytes-extent;offset+=8){
        const auto end=metadataWord(data+offset+24);
        if(end/1000*1000!=v4lTime)continue;
        const auto sof=metadataWord(data+offset),eof=metadataWord(data+offset+8),isr=metadataWord(data+offset+16);
        if(!sof||sof>end||end-sof>100000000u||!isr||isr>end||end-isr>100000000u)continue;
        if(eof&&(eof<sof||eof>end||end-eof>100000000u))continue;
        if(found)throw std::runtime_error("Camera capture metadata is ambiguous");
        CaptureMetadata result;result.offset=offset;result.sofRaw=sof;result.eofRaw=eof;
        result.sofInterruptRaw=isr;result.eofInterruptRaw=end;
        const auto signedWord=metadataWord(data+offset+32);std::memcpy(&result.ispToRaw,&signedWord,8);
        std::memcpy(result.sensor.data(),data+offset+40,result.sensor.size());found=result;
    }
    return found;
}
inline std::string sensorMetadataHex(const CaptureMetadata& metadata){
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(metadata.sensor.size()*2);
    for(auto value:metadata.sensor){result+=hex[value>>4];result+=hex[value&15];}return result;
}
} // namespace room_camera_api
