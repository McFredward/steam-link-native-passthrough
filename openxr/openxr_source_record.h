#pragma once
#include <array>
#include <cstdint>
#include <cstring>

// The application side of the SBN1 source declaration
// (docs/protocols/application-source-v1.md), as the OpenXR layer speaks it on
// behalf of the process it is loaded into.
namespace stream_bridge::openxr {
constexpr std::size_t SourceBytes=128;
using SourceMessage=std::array<std::uint8_t,SourceBytes>;
using SourceNonce=std::array<std::uint8_t,16>;
inline bool nonzero(const SourceNonce& value)noexcept{for(auto b:value)if(b)return true;return false;}
inline SourceMessage sourceRequest(std::uint32_t pid,const SourceNonce& nonce,bool budget)noexcept{
    SourceMessage out{};std::memcpy(out.data(),"SBN1",4);
    auto w16=[&](unsigned at,std::uint16_t v){for(unsigned i=0;i<2;++i)out[at+i]=std::uint8_t(v>>(8*i));};
    auto w32=[&](unsigned at,std::uint32_t v){for(unsigned i=0;i<4;++i)out[at+i]=std::uint8_t(v>>(8*i));};
    w16(4,1);w16(6,budget?2:1);w16(8,128);w32(12,1);w32(16,pid);std::memcpy(out.data()+24,nonce.data(),16);
    return out;
}
// A reply is valid only for this exact request and server. A valid reply
// without flag1 means the sender is present but has not admitted this process
// yet (it admits a process once it owns a native submission); it can follow on
// the same connection. Flag2 is reserved telemetry and never a prerequisite.
struct SourceReply {bool valid=false,granted=false,budget=false;SourceNonce binding{};std::uint64_t generation=0;};
inline SourceReply readSourceReply(const SourceMessage& input,std::uint32_t pid,std::uint32_t server,const SourceNonce& nonce,bool budget)noexcept{
    auto u16=[&](unsigned p){return unsigned(input[p])|(unsigned(input[p+1])<<8);};
    auto u32=[&](unsigned p){return std::uint32_t(input[p])|(std::uint32_t(input[p+1])<<8)|(std::uint32_t(input[p+2])<<16)|(std::uint32_t(input[p+3])<<24);};
    SourceReply out;
    if(!pid||!server||!nonzero(nonce)||std::memcmp(input.data(),"SBN1",4)||u16(4)!=2||u16(6)!=(budget?2u:1u)||u16(8)!=128||u32(12)!=1||
       u32(16)!=pid||u32(20)!=server||std::memcmp(input.data()+24,nonce.data(),16))return out;
    for(unsigned i=64;i<128;++i)if(input[i])return out;
    const unsigned flags=u16(10);if(flags&~(budget?7u:3u))return out;
    std::memcpy(out.binding.data(),input.data()+40,16);for(unsigned i=0;i<8;++i)out.generation|=std::uint64_t(input[56+i])<<(8*i);
    out.granted=flags&1;out.budget=flags&4;
    if(out.granted){if(!nonzero(out.binding)||!out.generation||out.budget!=budget)return out;}
    else if(out.budget||nonzero(out.binding)||out.generation)return out;
    out.valid=true;return out;
}
} // namespace stream_bridge::openxr
