#pragma once
// Own bounded DXBC inspector. Only hashes of reviewed executable/signature
// chunks are retained; no third-party shader bytes or disassembly are shipped.
#include "receiver/sha256.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace stream_bridge::sender {
enum class NativeShader : unsigned {Unknown,FullscreenVertex,CopyView,CopyViewCas,Luma,LumaLegacy,Chroma,ChromaLegacy,ChromaPack};
struct ShaderFingerprint {NativeShader kind=NativeShader::Unknown;std::array<std::uint8_t,32> hash{};};
inline bool dxbcFingerprint(const void* input,std::size_t size,std::array<std::uint8_t,32>& hash)noexcept{
    if(!input||size<44||size>65536)return false;
    const auto* bytes=static_cast<const std::uint8_t*>(input);
    const auto word=[&](std::size_t at){return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);};
    if(std::memcmp(bytes,"DXBC",4)||word(20)!=1||word(24)!=size)return false;
    const auto count=word(28);if(count<3||count>16||32+4*count>size)return false;
    struct Chunk{std::uint32_t at=0,size=0;};std::array<Chunk,16> chunks{};std::array<Chunk,3> selected{};
    for(unsigned i=0;i<count;++i){
        const auto at=word(32+4*i);if((at&3u)||at<32+4*count||at>size-8)return false;
        const auto n=word(at+4);if(n>size-at-8)return false;
        for(unsigned j=0;j<i;++j)if(at<chunks[j].at+8+chunks[j].size&&chunks[j].at<at+8+n)return false;
        chunks[i]={at,n};int target=-1;
        if(!std::memcmp(bytes+at,"SHEX",4)||!std::memcmp(bytes+at,"SHDR",4))target=0;
        else if(!std::memcmp(bytes+at,"ISGN",4))target=1;
        else if(!std::memcmp(bytes+at,"OSGN",4))target=2;
        else if(std::memcmp(bytes+at,"RDEF",4)&&std::memcmp(bytes+at,"STAT",4)&&std::memcmp(bytes+at,"SDBG",4)&&std::memcmp(bytes+at,"SPDB",4)&&std::memcmp(bytes+at,"ILDN",4))return false;
        if(target>=0){if(selected[static_cast<unsigned>(target)].at)return false;selected[static_cast<unsigned>(target)]={at,n};}
    }
    for(const auto& chunk:selected)if(!chunk.at||chunk.size<8)return false;
    if((selected[0].size&3u)||word(selected[0].at+12)!=selected[0].size/4)return false;
    frame_receiver::Sha256 digest;for(const auto& chunk:selected)digest.update(bytes+chunk.at,8+chunk.size);
    hash=digest.finish();return true;
}
inline NativeShader classifyNativeShader(const void* bytes,std::size_t size,const ShaderFingerprint* contracts,std::size_t count)noexcept{
    std::array<std::uint8_t,32> hash{};if(!contracts||count>32||!dxbcFingerprint(bytes,size,hash))return NativeShader::Unknown;
    NativeShader result=NativeShader::Unknown;
    for(std::size_t i=0;i<count;++i)if(hash==contracts[i].hash){if(result!=NativeShader::Unknown)return NativeShader::Unknown;result=contracts[i].kind;}
    return result;
}
inline std::array<std::uint8_t,32> shaderDigest(const char* hex)noexcept{
    std::array<std::uint8_t,32> result{};
    const auto nibble=[](char c){return c>='0'&&c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)result[i]=std::uint8_t(nibble(hex[2*i])*16+nibble(hex[2*i+1]));
    return result;
}
inline const std::array<ShaderFingerprint,8>& nativeShaderContracts()noexcept{
    static const std::array<ShaderFingerprint,8> contracts{{
        {NativeShader::FullscreenVertex,shaderDigest("e35b9b6b0ccb7e169e2fc5e233d7fee89df4b4928c3b25460ff9d76b4bbb3809")},
        {NativeShader::CopyView,shaderDigest("c9f1b48ffb42c5d34fdf0cfa93dc5b79b635c223735154a012c67ee5cd478e99")},
        {NativeShader::CopyViewCas,shaderDigest("c5f4467e016675cdd41269ab135932cf16ca00bc672d6aba775c363a0fdbd803")},
        {NativeShader::Luma,shaderDigest("46d78f7c82fd321aaf7725c911a2fb901b87443bc6d1f0c095e50a50b9072be5")},
        {NativeShader::LumaLegacy,shaderDigest("cc53a88f23a6b582501f7f1a742c8bcddc884eda97de7f8b9116351f624f3955")},
        {NativeShader::Chroma,shaderDigest("6063c74433659913836d2d5de149b7e21fcb37a0688d7209be2610a472665abc")},
        {NativeShader::ChromaLegacy,shaderDigest("fefe3273bae76a2ed74c0dec8579f6d00614e06454cf67336f3a936ba9bff89d")},
        {NativeShader::ChromaPack,shaderDigest("df33300023fcfb9a1c87107b8f3fcb70d10028f9978116caf0ddb403d5b1527a")}
    }};return contracts;
}
} // namespace stream_bridge::sender
