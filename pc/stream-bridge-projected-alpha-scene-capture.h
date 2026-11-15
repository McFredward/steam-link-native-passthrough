#pragma once
// Unshipped cold DXBC transformation. No original executable instruction is
// changed. Only a private SV_Target1 declaration and alpha output are added.
// Caller still needs an exact reviewed shader contract and image/frame proof.
#include "stream-bridge-native-shader.h"
#include <vector>
#include <algorithm>
namespace stream_bridge::sender::projected_alpha::scene_capture {
inline std::uint32_t load(const std::uint8_t* p)noexcept{
    return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
}
inline void store(std::uint8_t* p,std::uint32_t value)noexcept{
    for(unsigned i=0;i<4;++i)p[i]=std::uint8_t(value>>(8*i));
}
// DXBC's public retail checksum uses MD5 compression with its own final-block
// layout. This is container validation, never a security/identity primitive.
// Format reference: Microsoft's DirectXShaderCompiler/lib/DxilHash/DxilHash.cpp.
inline std::array<std::uint8_t,16> checksum(const std::uint8_t* bytes,std::size_t size)noexcept{
    constexpr std::array<std::uint32_t,64> constants{{0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u}};
    constexpr unsigned shifts[4][4]{{7,12,17,22},{5,9,14,20},{4,11,16,23},{6,10,15,21}};
    std::array<std::uint32_t,4> state{{0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u}};
    const auto block=[&](const std::uint8_t* data){
        const auto initial=state;auto a=state[0],b=state[1],c=state[2],d=state[3];
        for(unsigned i=0;i<64;++i){std::uint32_t f=0;unsigned at=0;
            if(i<16){f=(b&c)|(~b&d);at=i;}
            else if(i<32){f=(d&b)|(~d&c);at=(5*i+1)%16;}
            else if(i<48){f=b^c^d;at=(3*i+5)%16;}
            else {f=c^(b|~d);at=(7*i)%16;}
            const auto value=a+f+constants[i]+load(data+4*at);const auto shift=shifts[i/16][i%4];
            const auto next=b+((value<<shift)|(value>>(32-shift)));a=d;d=c;c=b;b=next;
        }
        state={{initial[0]+a,initial[1]+b,initial[2]+c,initial[3]+d}};
    };
    const auto complete=size/64;for(std::size_t i=0;i<complete;++i)block(bytes+64*i);
    const auto remaining=size%64;std::array<std::uint8_t,64> last{};
    if(remaining<56){
        store(last.data(),std::uint32_t(size)<<3);std::memcpy(last.data()+4,bytes+64*complete,remaining);last[4+remaining]=0x80;
    }else{
        std::memcpy(last.data(),bytes+64*complete,remaining);last[remaining]=0x80;block(last.data());last={};
        store(last.data(),std::uint32_t(size)<<3);
    }
    store(last.data()+60,1u|(std::uint32_t(size)<<1));block(last.data());
    std::array<std::uint8_t,16> output{};for(unsigned i=0;i<4;++i)store(output.data()+i*4,state[i]);return output;
}
// Only the reviewed scene tail is supported here: one native float4 SV_Target0,
// a final scalar MOVC o0.w, condition, literal1, temporary-alpha followed by RET.
// No branch/constant/source/geometry rewrite and no runtime HLSL compilation.
// Refuse all other forms, including the distinct alpha-dependent resolve.
inline bool derive(const void* input,std::size_t size,std::vector<std::uint8_t>& output){
    output.clear();std::array<std::uint8_t,32> fingerprint{};
    if(!dxbcFingerprint(input,size,fingerprint))return false;
    const auto* original=static_cast<const std::uint8_t*>(input);
    const auto originalChecksum=checksum(original+20,size-20);
    if(std::memcmp(original+4,originalChecksum.data(),16))return false;
    const auto count=load(original+28);unsigned executable=16,signature=16;
    struct Chunk {std::array<std::uint8_t,4> name{};std::vector<std::uint8_t> data;};
    std::vector<Chunk> chunks;chunks.reserve(count);
    for(unsigned i=0;i<count;++i){const auto offset=load(original+32+4*i),length=load(original+offset+4);Chunk chunk;
        std::copy_n(original+offset,4,chunk.name.data());chunk.data.assign(original+offset+8,original+offset+8+length);
        if(!std::memcmp(chunk.name.data(),"SHEX",4)||!std::memcmp(chunk.name.data(),"SHDR",4))executable=i;
        if(!std::memcmp(chunk.name.data(),"OSGN",4))signature=i;
        chunks.push_back(std::move(chunk));
    }
    if(executable>=count||signature>=count)return false;
    auto& sig=chunks[signature].data;
    // OSGN header + one 24-byte record + bounded semantic string.
    if(sig.size()<36||load(sig.data())!=1||load(sig.data()+4)!=8||load(sig.data()+12)||
        (load(sig.data()+16)!=0&&load(sig.data()+16)!=64)||load(sig.data()+20)!=3||load(sig.data()+24)||
        (load(sig.data()+28)&255u)!=15)return false;
    const auto name=load(sig.data()+8);
    if(name<32||name>=sig.size()||sig.size()-name<10||std::memcmp(sig.data()+name,"SV_Target",10))return false;
    auto& code=chunks[executable].data;const auto length=code.size()/4;
    if(length<14||(load(code.data())!=0x50u&&load(code.data())!=0x40u)||load(code.data()+4)!=length)return false;
    std::vector<std::uint32_t> words(length);for(std::size_t i=0;i<length;++i)words[i]=load(code.data()+4*i);
    std::size_t at=2,declaration=0,tail=0,previous=0;unsigned outputs=0,returns=0;
    while(at<length){const auto token=words[at],opcode=token&2047u,n=(token>>24)&127u;
        if(!n||n>length-at)return false;
        if(opcode==101){
            if(n!=3||words[at+1]!=0x001020f2u||words[at+2])return false;
            ++outputs;declaration=at+n;
        }
        if(opcode==62){++returns;if(n!=1||at!=length-1)return false;tail=previous;}
        previous=at;at+=n;
    }
    if(at!=length||outputs!=1||returns!=1||!declaration||tail<=declaration||length-tail!=10)return false;
    // Scalar unmodified temporary operands: no relative addressing, modifiers,
    // extensions or extra source words. SV_Target1 is private alpha only.
    if(words[tail]!=0x09000037u||words[tail+1]!=0x00102082u||words[tail+2]||
        (words[tail+3]&~0x30u)!=0x0010000au||words[tail+5]!=0x00004001u||words[tail+6]!=0x3f800000u||
        (words[tail+7]&~0x30u)!=0x0010000au)return false;
    std::vector<std::uint32_t> rewritten;rewritten.reserve(length+16);
    for(std::size_t i=0;i<length;++i){
        if(i==declaration)rewritten.insert(rewritten.end(),{0x03000065u,0x001020f2u,1u});
        if(i==tail){
            rewritten.insert(rewritten.end(),{0x08000036u,0x001020f2u,1u,0x00004002u,0u,0u,0u,0u});
            rewritten.insert(rewritten.end(),{0x05000036u,0x00102082u,1u,words[tail+7],words[tail+8]});
        }
        rewritten.push_back(words[i]);
    }
    rewritten[1]=std::uint32_t(rewritten.size());code.resize(rewritten.size()*4);
    for(std::size_t i=0;i<rewritten.size();++i)store(code.data()+i*4,rewritten[i]);
    std::vector<std::uint8_t> signatureCopy(sig.size()+24);std::copy_n(sig.data(),32,signatureCopy.data());
    std::copy_n(sig.data()+8,24,signatureCopy.data()+32);std::copy(sig.begin()+32,sig.end(),signatureCopy.begin()+56);
    store(signatureCopy.data(),2);store(signatureCopy.data()+8,name+24);store(signatureCopy.data()+32,name+24);
    store(signatureCopy.data()+36,1);store(signatureCopy.data()+48,1);sig.swap(signatureCopy);
    output.resize(32+4*count);std::memcpy(output.data(),"DXBC",4);store(output.data()+20,1);store(output.data()+28,count);
    for(unsigned i=0;i<count;++i){const auto offset=output.size();store(output.data()+32+4*i,std::uint32_t(offset));
        output.insert(output.end(),chunks[i].name.begin(),chunks[i].name.end());const auto n=output.size();output.resize(n+4);
        store(output.data()+n,std::uint32_t(chunks[i].data.size()));output.insert(output.end(),chunks[i].data.begin(),chunks[i].data.end());
        while(output.size()%4)output.push_back(0);
    }
    if(output.size()>65536){output.clear();return false;}
    store(output.data()+24,std::uint32_t(output.size()));const auto hash=checksum(output.data()+20,output.size()-20);
    std::copy(hash.begin(),hash.end(),output.begin()+4);return true;
}
} // namespace stream_bridge::sender::projected_alpha::scene_capture
