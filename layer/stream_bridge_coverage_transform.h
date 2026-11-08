#pragma once
// Cold transformation of the actual native pipeline's reviewed fragment.
// No files, compiler, sockets or device operations. Results are never cached
// across native shader generations or borrowed from installation-time output.
#include "pixel_ssbo_clone_contract.h"
#include "stream_bridge_coverage_transform_data.h"

namespace stream_bridge_coverage_transform {
namespace clone=frame_pixel_ssbo_clone;
struct Fragment {
    std::array<std::uint32_t,clone::ShaderBytes/4> words{};
    std::size_t bytes=0;
};
inline bool transform(const clone::ShaderDefinition& original,bool cameraOnly,Fragment& out) noexcept {
    out={};if(!original.complete)return false;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=original.bytes;info.pCode=original.words.data();
    clone::ShaderDefinition checked;clone::Refusal why;
    if(!clone::captureShader(info,clone::ShaderRole::Fragment,checked,why))return false;
    const auto& program=checked.bytes==6620?(cameraOnly?detail::StableCamera:detail::StableCoverage):
                                           (cameraOnly?detail::CurrentCamera:detail::CurrentCoverage);
    std::size_t source=0,used=0;
    const auto append=[&](const std::uint32_t* words,std::size_t count){
        if(count>out.words.size()-used)return false;
        std::memcpy(out.words.data()+used,words,count*4);used+=count;return true;
    };
    for(std::size_t n=0;n<program.count;++n){const auto& p=program.patches[n];
        if(p.offset<source||p.offset>checked.bytes/4||p.remove>checked.bytes/4-p.offset||
           !append(checked.words.data()+source,p.offset-source)||!append(program.additions+p.first,p.count))return false;
        source=p.offset+p.remove;
    }
    if(!append(checked.words.data()+source,checked.bytes/4-source)||used*4!=program.bytes)return false;
    out.words[3]=program.bound;
    frame_receiver::Sha256 digest;digest.update(out.words.data(),used*4);
    if(!clone::digestMatches(digest.finish(),program.sha256)){out={};return false;}
    out.bytes=used*4;return true;
}
inline bool pairedStages(const clone::ShaderDefinition& vertex,const clone::ShaderDefinition& fragment) noexcept {
    // Actual reviewed stage pair, not a SteamVR version string or a mixed
    // recipe containing stages from two different native pipeline contracts.
    if(!vertex.complete||!fragment.complete)return false;
    clone::ShaderDefinition vs,ps;clone::Refusal why;
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=vertex.bytes;info.pCode=vertex.words.data();
    if(!clone::captureShader(info,clone::ShaderRole::Vertex,vs,why))return false;
    info.codeSize=fragment.bytes;info.pCode=fragment.words.data();
    if(!clone::captureShader(info,clone::ShaderRole::Fragment,ps,why))return false;
    return (vs.bytes==9460&&ps.bytes==6620)||(vs.bytes==9508&&ps.bytes==4532);
}
} // namespace stream_bridge_coverage_transform
