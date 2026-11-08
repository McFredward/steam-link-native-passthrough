#pragma once
// Diagnostic metadata only. This cannot admit a shader, device or game frame.
// Never emit a native body, pointer, descriptor handle or scene/camera data.
#include "pixel_ssbo_clone_contract.h"
namespace stream_bridge_cold_admission {
enum class ChainEnd : std::uint8_t {Complete,Cycle,Bounds};
struct DeviceChain {
    std::array<std::uint32_t,frame_pixel_ssbo_clone::MaxChain> types{};
    std::uint32_t count=0;
    ChainEnd end=ChainEnd::Complete;
};
inline DeviceChain deviceChain(const VkDeviceCreateInfo& info) noexcept {
    DeviceChain out;std::array<const VkBaseInStructure*,frame_pixel_ssbo_clone::MaxChain> seen{};
    for(auto* node=static_cast<const VkBaseInStructure*>(info.pNext);node;node=node->pNext){
        for(std::uint32_t i=0;i<out.count;++i)if(seen[i]==node){out.end=ChainEnd::Cycle;return out;}
        if(out.count==seen.size()){out.end=ChainEnd::Bounds;return out;}
        seen[out.count]=node;out.types[out.count++]=std::uint32_t(node->sType);
    }
    return out;
}
struct Shader {
    std::array<char,32> entry{};
    std::array<std::uint8_t,32> sha256{};
    std::size_t bytes=0;
};
inline bool distortionShader(const VkShaderModuleCreateInfo& info,Shader& out) noexcept {
    out={};Shader candidate;
    if(info.sType!=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO||!info.pCode||info.codeSize<20||
       info.codeSize>65536||info.codeSize%4||info.pCode[0]!=0x07230203u)return false;
    const auto* words=info.pCode;const auto length=info.codeSize/4;
    for(std::size_t at=5;at<length;){const auto count=words[at]>>16,op=words[at]&65535u;
        if(!count||count>length-at)return false;
        if(op==15&&count>=4&&(words[at+1]==0||words[at+1]==4)){
            const auto* name=reinterpret_cast<const char*>(words+at+3);
            const std::size_t bound=(count-3)*4;std::size_t n=0;
            for(;n<bound&&n<candidate.entry.size()&&name[n];++n){const auto c=static_cast<unsigned char>(name[n]);
                if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;
            }
            if(n==bound||n==candidate.entry.size())return false;
            const bool target=(n>=10&&(!std::memcmp(name,"PS_DISTORT",10)||!std::memcmp(name,"VS_DISTORT",10)))||
                              (n>=11&&!std::memcmp(name,"VS_FOVEATED",11));
            if(target){if(candidate.entry[0])return false;std::memcpy(candidate.entry.data(),name,n);}
        }
        at+=count;
    }
    if(!candidate.entry[0])return false;
    frame_receiver::Sha256 digest;digest.update(words,info.codeSize);candidate.sha256=digest.finish();candidate.bytes=info.codeSize;out=candidate;return true;
}
inline std::array<char,65> hex(const std::array<std::uint8_t,32>& digest) noexcept {
    std::array<char,65> result{};constexpr char digits[]="0123456789abcdef";
    for(std::size_t n=0;n<digest.size();++n){result[n*2]=digits[digest[n]>>4];result[n*2+1]=digits[digest[n]&15];}return result;
}
} // namespace stream_bridge_cold_admission
