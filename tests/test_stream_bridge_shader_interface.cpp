// Owned synthetic SPIR-V declarations; no native body, GPU or frame authority.
#include "layer/stream_bridge_shader_interface.h"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace s=stream_bridge_shader_interface;
namespace {
unsigned checks=0;
void check(bool yes,const char* name){++checks;if(!yes)throw std::runtime_error(name);}
using Words=std::vector<std::uint32_t>;
void instruction(Words& words,unsigned op,std::initializer_list<std::uint32_t> args){
    words.push_back(std::uint32_t((args.size()+1)<<16)|op);words.insert(words.end(),args);
}
Words module(bool vertex){
    Words words{0x07230203,0x10000,0,32,0};
    const auto at=words.size();words.resize(at+6);
    words[at]=(9u<<16)|15;words[at+1]=vertex?0u:4u;words[at+2]=1;
    std::memcpy(words.data()+at+3,vertex?"VS_DISTORT":"PS_DISTORT",11);
    words.insert(words.end(),{7,8,9});
    instruction(words,71,{7,30,0});instruction(words,71,{7,13});
    instruction(words,71,{8,30,2});instruction(words,71,{8,14});
    instruction(words,71,{9,11,9}); // Built-in layer, absent from user varyings.
    instruction(words,22,{2,32});instruction(words,21,{3,32,0});
    instruction(words,23,{4,2,4});
    instruction(words,32,{5,vertex?3u:1u,4});instruction(words,32,{6,vertex?3u:1u,3});
    instruction(words,59,{5,7,vertex?3u:1u});instruction(words,59,{6,8,vertex?3u:1u});
    instruction(words,59,{6,9,vertex?3u:1u});return words;
}
bool capture(const Words& words,bool vertex,s::Interface& out){return s::capture(words.data(),words.size()*4,vertex,out);}
void change(Words& words,unsigned op,unsigned operand,std::uint32_t value){
    for(std::size_t at=5;at<words.size();at+=words[at]>>16)if((words[at]&65535)==op){words[at+operand]=value;return;}
    throw std::runtime_error("synthetic instruction absent");
}
void controls(){
    auto vs=module(true),ps=module(false);s::Interface output,input;
    check(capture(vs,true,output)&&output.count==2,"vertex exports reflect scalar/vector locations and skip built-ins");
    check(capture(ps,false,input)&&s::compatible(output,input),"matching locations, integer/vector types and interpolation agree");
    auto moved=input;moved.fields[1].location=3;
    check(!s::compatible(output,moved),"different layer-location cannot borrow matching vector locations");
    moved=input;moved.fields[1].kind=1;moved.fields[1].lanes=4;
    check(!s::compatible(output,moved),"vec4 output cannot satisfy uint input at same location");
    moved=input;moved.fields[1].width=64;
    check(!s::compatible(output,moved),"scalar widths must match");
    moved=input;moved.fields[0].interpolation=0;
    check(!s::compatible(output,moved),"noperspective and flat declarations are retained");
    moved=input;moved.fields[0].component=1;
    check(!s::compatible(output,moved),"component cannot silently shift a varying");
    moved=input;moved.complete=false;
    check(!s::compatible(output,moved),"partial facts never establish a compatible interface");
    auto bad=vs;change(bad,21,3,1);
    check(capture(bad,true,output)&&!s::compatible(output,input),"signed and unsigned integers remain distinct");
    check(capture(vs,true,output),"restore original vertex facts");
    bad=vs;change(bad,23,3,3);
    check(capture(bad,true,output)&&!s::compatible(output,input),"vector length must match");
    bad=vs;change(bad,71,3,2);
    check(!capture(bad,true,output)&&!output.complete&&!output.count,"duplicate user location refuses without partial output");
    bad=vs;bad[5]=0;
    check(!capture(bad,true,output),"zero-size instruction is bounded");
    check(!capture(vs,false,output),"entry execution model must match requested stage");
    bad=vs;bad[8]=0;
    check(!capture(bad,true,output),"unrelated or malformed entry name refuses");
    bad=vs;bad[3]=2049;
    check(!capture(bad,true,output),"ID inventory has a fixed cold budget");
    bad=vs;change(bad,59,2,32);
    check(!capture(bad,true,output),"out-of-bound result identity refuses");
    check(!s::capture(reinterpret_cast<const std::uint32_t*>(std::uintptr_t(1)),65540,true,output),
          "oversized source rejected before reading its pointer");
}
}
int main(){try{controls();std::cout<<"stage interface: "<<checks<<" checks PASS; native_authority=false\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
