#pragma once
// The native colour-key variant is a separate existing feature family, not a
// SteamVR build. Explicit coverage must use the corresponding unkeyed decoded
// video colour. Never turn RGB classification into a game alpha source.
// Both programs are independently attested. Native code remains private
// runtime input; only numeric operand roles and logic digests are distributed.
#include "native_shader_contract.h"
#include "../receiver/sha256.h"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
namespace stream_bridge_native_colour {
constexpr std::size_t ColourBytes=4532,MaximumBytes=12288,KeyedBytes=28632;
inline constexpr char ColourSha256[]="a42daff46b5a155bc66ba53361face163fb1bd39260cb374430c1732c0ecfd1f";
inline constexpr char KeyedSha256[]="3d458ebb2af4adfa571ba4f6abccdd3ef164fd72c8148adafd81afb2f7c96d13";
inline std::array<std::uint32_t,MaximumBytes/4> colourWords{};
inline std::atomic<bool> colourReady{false};
inline bool digest(const void* bytes,std::size_t size,const char* expected)noexcept{
    frame_receiver::Sha256 hash;hash.update(bytes,size);const auto value=hash.finish();
    constexpr char alphabet[]="0123456789abcdef";
    for(unsigned i=0;i<value.size();++i)
        if(alphabet[value[i]>>4]!=expected[i*2]||alphabet[value[i]&15]!=expected[i*2+1])return false;
    return true;
}
struct Program {
    stream_bridge_shader_contract::Contract contract;
    std::size_t bytes;
    const char* sha256;
};
// Pure bounded selection core, also exercised with independently owned tiny
// programs. Production supplies only the two fixed reviewed families below.
inline bool copyAttestedAlternative(const std::uint32_t* words,std::size_t bytes,Program keyed,Program colour,
    const std::uint32_t* cached,std::size_t cachedBytes,std::uint32_t* output,std::size_t capacity,std::size_t& used)noexcept{
    used=0;if(!output||!cached||!keyed.sha256||!colour.sha256||!colour.bytes||colour.bytes%4||
        cachedBytes!=colour.bytes||capacity<colour.bytes/4||!digest(cached,cachedBytes,colour.sha256))return false;
    auto canonical=std::unique_ptr<std::uint32_t[]>(new(std::nothrow)std::uint32_t[65536/4]);
    if(!canonical)return false;
    std::size_t count=0;
    if(!stream_bridge_shader_contract::normalize(words,bytes,keyed.contract,canonical.get(),65536/4,count)||
        count*4!=keyed.bytes||!digest(canonical.get(),count*4,keyed.sha256))return false;
    used=colour.bytes/4;std::memcpy(output,cached,colour.bytes);return true;
}
// Once, on the verified compositor's cold constructor owner before its worker.
// Failed candidates do not publish partial data. No file I/O belongs here or in
// shader admission; the process owner supplies bounded current-runtime bytes.
inline bool prepareColourCold(const std::uint32_t* words,std::size_t bytes)noexcept{
    if(colourReady.load(std::memory_order_acquire))return true;
    std::array<std::uint32_t,MaximumBytes/4> candidate{};std::size_t used=0;
    if(!stream_bridge_shader_contract::normalize(words,bytes,stream_bridge_shader_contract::fragmentBeta(),
            candidate.data(),candidate.size(),used)||used*4!=ColourBytes||!digest(candidate.data(),used*4,ColourSha256))return false;
    colourWords=candidate;colourReady.store(true,std::memory_order_release);return true;
}
// Only the independently reviewed keyed family can select the cached reviewed
// unkeyed family. Matching filenames, interfaces or a caller boolean cannot.
// The caller still validates pipeline layouts, varyings, exact video images,
// current frame/eye/mask and camera authority before any replacement is used.
inline bool copyUnkeyed(const std::uint32_t* words,std::size_t bytes,
    std::uint32_t* output,std::size_t capacity,std::size_t& used)noexcept{
    used=0;if(!output||capacity<ColourBytes/4||!colourReady.load(std::memory_order_acquire))return false;
    return copyAttestedAlternative(words,bytes,{stream_bridge_shader_contract::fragmentChromaKeyBeta(),KeyedBytes,KeyedSha256},
        {stream_bridge_shader_contract::fragmentBeta(),ColourBytes,ColourSha256},colourWords.data(),ColourBytes,output,capacity,used);
}
} // namespace stream_bridge_native_colour
