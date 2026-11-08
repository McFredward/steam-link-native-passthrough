// Synthetic Vulkan creation inputs; no GPU, native code or frame authority.
#include "layer/pixel_ssbo_definition_journal.h"
#include "layer/stream_bridge_cold_admission.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
namespace c=frame_pixel_ssbo_clone;
namespace j=frame_pixel_ssbo_journal;
namespace diag=stream_bridge_cold_admission;
namespace {
unsigned checks=0;
void check(bool yes,const char* name){++checks;if(!yes)throw std::runtime_error(name);}
template<class T>T object(std::uint64_t value){T out{};std::memcpy(&out,&value,sizeof(out));return out;}
void devices(){
    VkPhysicalDeviceFeatures features{};VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.pEnabledFeatures=&features;
    const auto device=object<VkDevice>(1);j::Event event;
    auto strict=std::make_unique<j::Journal>();
    check(!strict->createDevice(device,ci,VK_SUCCESS,&event)&&event.contract.reason==c::Reason::FeatureDisabled,"storage probe still requires enabled fragment stores");
    auto sampled=std::make_unique<j::Journal>(1,c::ProbeInterface{},j::PendingCount1::RetainFacts,c::ShaderAccess::SampledFragment);
    check(sampled->createDevice(device,ci,VK_SUCCESS,&event),"sampled fragment needs no fragment stores");
    auto facts=sampled->inspectStats().feature;
    check(facts.complete&&facts.observed&&!facts.enabled&&facts.raw==VK_FALSE&&!facts.nativeAuthority,"actual disabled bit is retained, not promoted to enabled authority");
    VkDescriptorSetLayoutBinding binding{9,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
    VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=1;set.pBindings=&binding;
    check(sampled->createSet(object<VkDescriptorSetLayout>(2),set,VK_SUCCESS,&event),"sampled device can retain native descriptor facts");
    ci.pEnabledFeatures=nullptr;
    c::FeatureFacts observed;c::Refusal why;
    check(c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&observed.complete&&!observed.observed&&!observed.enabled,"absent optional storage declaration is valid for sampling");
    check(!c::enabledFeature(ci,true,observed,why)&&why.reason==c::Reason::FeatureAbsent,"legacy factory remains strict when storage absent");
    ci.pEnabledFeatures=&features;features.fragmentStoresAndAtomics=2;
    check(!c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&why.reason==c::Reason::FeatureValue,"malformed known feature still refuses");
    features.fragmentStoresAndAtomics=0;
    VkPhysicalDeviceFeatures2 second{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    ci.pNext=&second;
    check(!c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&why.reason==c::Reason::FeatureConflict,"conflicting legacy and Features2 declarations remain refused");
    ci.pEnabledFeatures=nullptr;
    check(c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&observed.source==c::FeatureSource::Features2&&!observed.enabled,"Features2 disabled storage remains actual disabled state");
    second.pNext=&second;
    check(!c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&why.reason==c::Reason::ChainCycle,"feature-chain cycle stays bounded");
    second.pNext=nullptr;VkBaseInStructure unknown{VkStructureType(0x6fffffff),nullptr};ci.pNext=&unknown;
    check(!c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess::SampledFragment)&&why.reason==c::Reason::Extension,"unknown payload is not reinterpreted or ignored");
    ci.pNext=nullptr;
    check(!c::captureDeviceFeatures(ci,false,observed,why,c::ShaderAccess::SampledFragment)&&why.reason==c::Reason::NativeCreationFailed,"failed original device does not create facts");
    check(!c::captureDeviceFeatures(ci,true,observed,why,c::ShaderAccess(255)),"invalid access policy refuses");
}
std::vector<std::uint32_t> module(const char* name){
    const auto n=std::strlen(name);std::vector<std::uint32_t> out{0x07230203,0x10000,0,2,0};
    const auto words=(n+1+3)/4;out.push_back(std::uint32_t((words+3)<<16)|15);out.push_back(4);out.push_back(1);
    const auto at=out.size();out.resize(at+words);std::memcpy(out.data()+at,name,n);return out;
}
void metadata(){
    auto words=module("PS_DISTORT");VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.pCode=words.data();ci.codeSize=words.size()*4;
    diag::Shader out;
    check(diag::distortionShader(ci,out)&&std::strcmp(out.entry.data(),"PS_DISTORT")==0&&out.bytes==ci.codeSize,"bounded native entry metadata only");
    const auto digest=out.sha256;words[2]=123;
    check(diag::distortionShader(ci,out)&&out.sha256!=digest,"raw digest distinguishes compiler metadata for file correlation");
    check(diag::hex(out.sha256)[64]==0,"hash rendering is bounded and terminated");
    auto other=module("PS_UNRELATED");ci.pCode=other.data();ci.codeSize=other.size()*4;
    check(!diag::distortionShader(ci,out)&&out.bytes==0&&out.entry[0]==0,"unrelated shader is not logged as a target");
    auto invalid=module("PS_DISTORT_bad%name");ci.pCode=invalid.data();ci.codeSize=invalid.size()*4;
    check(!diag::distortionShader(ci,out)&&!out.bytes,"untrusted name characters are not emitted");
    ci.pCode=words.data();ci.codeSize=words.size()*4;words[5]=0;
    check(!diag::distortionShader(ci,out)&&!out.bytes,"zero-length instruction refuses");
    words=module("VS_FOVEATED");ci.pCode=words.data();ci.codeSize=words.size()*4;
    check(diag::distortionShader(ci,out)&&!std::strcmp(out.entry.data(),"VS_FOVEATED"),"alternate native vertex name is diagnostic, not shader admission");
    auto second=module("PS_DISTORT");words.insert(words.end(),second.begin()+5,second.end());ci.pCode=words.data();ci.codeSize=words.size()*4;
    check(!diag::distortionShader(ci,out)&&!out.bytes&&out.entry[0]==0,"ambiguous target entries do not create a misleading identity");
    ci.pCode=reinterpret_cast<const std::uint32_t*>(std::uintptr_t(1));ci.codeSize=65540;
    check(!diag::distortionShader(ci,out),"oversized input rejected before touching a pointer");
}
}
int main(){try{devices();metadata();std::cout<<"sampled device and cold metadata: "<<checks<<" checks PASS; native_authority=false\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
