// Public refusal controls; optional private actual-stage replay. No native code
// or generated Valve-derived shader is embedded in this test.
#include "layer/stream_bridge_coverage_transform.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace t=stream_bridge_coverage_transform;
namespace c=frame_pixel_ssbo_clone;
namespace {
unsigned checks=0;
void check(bool yes,const char* reason){++checks;if(!yes)throw std::runtime_error(reason);}
std::vector<std::uint32_t> load(const char* path){
    std::ifstream f(path,std::ios::binary|std::ios::ate);check(bool(f),"private input readable");
    const auto n=f.tellg();check(n>=20&&n<=65536&&n%4==0,"bounded whole-word input");
    std::vector<std::uint32_t> w(std::size_t(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(w.data()),n);check(bool(f),"complete input read");return w;
}
c::ShaderDefinition capture(const std::vector<std::uint32_t>& words,c::ShaderRole role){
    c::ShaderDefinition definition;c::Refusal why;VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize=words.size()*4;info.pCode=words.data();check(c::captureShader(info,role,definition,why),"actual stage logic recognized");return definition;
}
void replay(const char* vertex,const char* fragment,const std::filesystem::path& directory,const char* name){
    const auto source=load(fragment),vertexSource=load(vertex);const auto vs=capture(vertexSource,c::ShaderRole::Vertex),ps=capture(source,c::ShaderRole::Fragment);
    check(t::pairedStages(vs,ps),"reviewed native stage pair");
    c::ProbeInterface profile;c::Refusal why;
    check(c::ProbeInterface::nativeCount1(ps.words.data(),ps.bytes,profile,why)&&profile.specializedCount1(),"all native descriptor accesses authorize only constant0");
    for(bool camera:{false,true}){
        t::Fragment output;check(t::transform(ps,camera,output),"actual current pipeline generates owned coverage");
        std::ofstream f(directory/(std::string(name)+(camera?"-camera.spv":"-coverage.spv")),std::ios::binary);
        f.write(reinterpret_cast<const char*>(output.words.data()),output.bytes);check(bool(f),"private output retained for independent comparison");
        auto damaged=ps;damaged.words[damaged.bytes/4-1]^=1;
        check(!t::transform(damaged,camera,output)&&!output.bytes,"changed native logic cannot borrow an earlier successful output");
        check(!t::pairedStages(vs,damaged),"shader booleans and extents cannot attest a changed stage");
    }
    auto moved=source;moved[2]^=12345; // Compiler metadata, not executable logic.
    moved.insert(moved.begin()+5,{(3u<<16)|5u,moved[3]-1,0x006f6f66u});
    auto normalized=capture(moved,c::ShaderRole::Fragment);
    check(normalized.bytes==ps.bytes&&std::equal(ps.words.begin(),ps.words.begin()+ps.bytes/4,normalized.words.begin()),"debug/compiler changes preserve canonical logic across both profiles");
    moved=source;
    for(std::size_t at=5;at<moved.size();){const auto count=moved[at]>>16,op=moved[at]&65535u;
        const auto* rule=stream_bridge_shader_contract::rule(op,count-1);check(rule!=nullptr,"all actual native operand roles known");
        for(unsigned n=0;n<count-1;++n)if(rule->idMask&(std::uint32_t(1)<<n))moved[at+1+n]=source[3]+17-moved[at+1+n];
        at+=count;
    }
    moved[3]+=18;normalized=capture(moved,c::ShaderRole::Fragment);
    check(normalized.sha256==ps.sha256,"complete native ID relocation preserves reviewed logic and literal operands");
    moved=vertexSource;
    for(std::size_t at=5;at<moved.size();){const auto count=moved[at]>>16,op=moved[at]&65535u;
        const auto* rule=stream_bridge_shader_contract::rule(op,count-1);check(rule!=nullptr,"all current native vertex operand roles known");
        for(unsigned n=0;n<count-1;++n)if(rule->idMask&(std::uint32_t(1)<<n))moved[at+1+n]=vertexSource[3]+17-moved[at+1+n];
        at+=count;
    }
    moved[3]+=18;normalized=capture(moved,c::ShaderRole::Vertex);
    check(normalized.sha256==vs.sha256,"complete vertex ID relocation preserves both native interfaces");
    const auto otherBytes=ps.bytes==6620?4532u:6620u;
    auto foreign=ps;foreign.bytes=otherBytes;check(!t::pairedStages(vs,foreign),"mismatched pair cannot activate");
}
}
int main(int argc,char** argv){try{
    check(argc==1||argc==6,"optional stableVS stablePS currentVS currentPS privateOutputDirectory");
    c::ShaderDefinition absent;t::Fragment output;
    check(!t::transform(absent,false,output)&&!output.bytes,"missing source refuses");
    absent.complete=true;absent.bytes=6620;absent.words[0]=0x07230203;
    check(!t::transform(absent,false,output)&&!output.bytes,"plausible source header is no executable proof");
    check(!t::pairedStages(absent,absent),"matching caller flags cannot attest native stages");
    if(argc==6){std::filesystem::create_directories(argv[5]);
        replay(argv[1],argv[2],argv[5],"stable");replay(argv[3],argv[4],argv[5],"current");
        replay(argv[1],argv[2],argv[5],"stable-return");
    }
    std::cout<<"coverage transform: "<<checks<<" checks PASS; headset_test=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
