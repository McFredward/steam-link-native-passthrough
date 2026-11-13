#pragma once
#include <array>
#include <cstdint>

namespace stream_bridge::sender {
// Diagnostic allocation ancestry only. These records cannot authorize a
// picture, a mask, a source-format lease or camera composition.
constexpr unsigned SourceAuditAllocations=192,SourceAuditSamples=32;
struct SourceAuditAllocation {
    std::uint64_t handle=0,generation=0;
    std::uint32_t pid=0,width=0,height=0,format=0,samples=0,arraySize=0;
};
struct SourceAuditInventory {
    std::uint64_t epoch=0;
    std::array<SourceAuditAllocation,SourceAuditAllocations> allocations{};
};
struct SourceAuditSample {
    std::uint64_t serial=0,inventoryEpoch=0,allocationGeneration=0;
    std::uint32_t shader=0,slot=0,pid=0,width=0,height=0,format=0;
    std::uint32_t arraySize=0,samples=0,view=0,viewFormat=0,ownerMatches=0;
    std::uint32_t outputWidth=0,outputHeight=0,outputFormat=0;
    std::uint32_t command=0,count=0,instances=0,start=0,firstInstance=0,argumentOffset=0;
    std::int32_t baseVertex=0;
    std::uint32_t vertexKind=0,programHashValid=0,programLinked=0;
    std::array<std::uint8_t,32> programHash{};
};
struct SourceAuditStatus {
    std::uint32_t pid=0,state=0,error=0,sampleCount=0,failureStage=0,remoteResult=0;
    std::uint32_t discoveryError=0,discoveryEntries=0,discoveryMatches=0,discoveryMisses=0,loadCalls=0,entryCalls=0;
    std::uint64_t devices=0,shaders=0,draws=0,recognizedDraws=0,dropped=0;
    std::array<std::uint64_t,3> streamShaderKinds{};
    std::uint64_t unknownPixelPrograms=0;
    // Indexed, plain, indexed-instanced, instanced, auto, indexed-indirect,
    // indirect and ExecuteCommandList. Only raster calls inspect bindings.
    std::array<std::uint64_t,8> rasterCalls{};
    std::array<SourceAuditSample,SourceAuditSamples> samples{};
};
// A numerical shared handle is considered only alongside retained registry
// generation and exact physical shape. Multiple owners remain ambiguous.
// Even a unique match is diagnostic ancestry, never current-frame proof.
inline SourceAuditSample auditSource(const SourceAuditInventory& inventory,
    std::uint64_t handle,std::uint32_t width,std::uint32_t height,
    std::uint32_t format,std::uint32_t samples,std::uint32_t arrays)noexcept {
    SourceAuditSample out{};out.inventoryEpoch=inventory.epoch;
    out.width=width;out.height=height;out.format=format;
    out.samples=samples;out.arraySize=arrays;
    if(!inventory.epoch||!handle||!width||!height||!format||!samples||!arrays)return out;
    for(const auto& a:inventory.allocations)if(a.handle==handle&&a.generation&&a.pid&&
        a.width==width&&a.height==height&&a.format==format&&a.samples==samples&&a.arraySize==arrays){
        ++out.ownerMatches;out.pid=a.pid;out.allocationGeneration=a.generation;
    }
    if(out.ownerMatches!=1){out.pid=0;out.allocationGeneration=0;}
    return out;
}
} // namespace stream_bridge::sender
