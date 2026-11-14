// Entirely owned DXBC containers and resource tokens; no third-party shader.
#include "pc/stream-bridge-native-composition.h"
#include "pc/stream-bridge-native-shader.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace stream_bridge::sender;
namespace {
unsigned checks=0;
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
void word(std::vector<std::uint8_t>& b,unsigned at,std::uint32_t value){for(unsigned i=0;i<4;++i)b[at+i]=std::uint8_t(value>>(8*i));}
std::vector<std::uint8_t> fixture(bool metadata=false){
    const unsigned count=metadata?4:3,first=32+4*count;
    std::vector<std::uint8_t> b(first+48+(metadata?16:0));std::memcpy(b.data(),"DXBC",4);word(b,20,1);word(b,24,static_cast<unsigned>(b.size()));word(b,28,count);
    const char* names[]{"SHEX","ISGN","OSGN","RDEF"};
    for(unsigned i=0;i<count;++i){const auto at=first+16*i;word(b,32+4*i,at);std::memcpy(b.data()+at,names[i],4);word(b,at+4,8);}
    word(b,first+8,0x50);word(b,first+12,2);return b;
}
void shaders(){
    auto b=fixture();std::array<std::uint8_t,32> hash{};check(dxbcFingerprint(b.data(),b.size(),hash),"owned bounded DXBC fingerprint");
    const ShaderFingerprint pin{NativeShader::CopyView,hash};
    check(classifyNativeShader(b.data(),b.size(),&pin,1)==NativeShader::CopyView,"actual code/signature digest identifies a reviewed family");
    auto debug=fixture(true);check(classifyNativeShader(debug.data(),debug.size(),&pin,1)==NativeShader::CopyView,"reflection/debug metadata changes no executable permission");
    word(debug,32,64);word(debug,36,48);check(classifyNativeShader(debug.data(),debug.size(),&pin,1)==NativeShader::CopyView,"physical chunk order does not change the executable contract");
    for(unsigned at:{52u,68u,84u}){auto changed=b;changed[at]^=1;check(classifyNativeShader(changed.data(),changed.size(),&pin,1)==NativeShader::Unknown,"changed executable or stage signature is refused");}
    for(std::size_t size=0;size<b.size();++size)check(!dxbcFingerprint(b.data(),size,hash),"every truncated container is refused");
    for(unsigned mode=0;mode<7;++mode){auto changed=b;
        if(mode==0)word(changed,28,17);
        if(mode==1)word(changed,32,0xffffffffu);
        if(mode==2)word(changed,36,44);
        if(mode==3)word(changed,48,0xfffffffcu);
        if(mode==4)word(changed,56,3);
        if(mode==5)std::memcpy(changed.data()+60,"SHEX",4);
        if(mode==6)std::memcpy(changed.data()+60,"ISG1",4);
        check(!dxbcFingerprint(changed.data(),changed.size(),hash),"malformed, overlapping, duplicate or unknown semantic chunks refuse admission");
    }
    const ShaderFingerprint duplicate[]{pin,pin};check(classifyNativeShader(b.data(),b.size(),duplicate,2)==NativeShader::Unknown,"ambiguous contract table never chooses a shader");
    check(classifyNativeShader(nullptr,b.size(),&pin,1)==NativeShader::Unknown,"null data has no shader authority");
}
NativeComposition full(){
    NativeComposition graph;check(graph.begin(17,23,64,64,3)&&graph.image(100),"native frame and retained composition image");
    check(graph.draw(1,{0,0,64,16})&&graph.draw(1,{0,16,64,16}),"two disjoint native views can belong to the same actual eye");
    check(!graph.complete(),"one eye cannot certify a stereo composition");
    check(graph.draw(2,{0,32,64,16})&&graph.draw(2,{0,48,64,16})&&graph.complete(),"all current eyes tile the complete native canvas");return graph;
}
void graphChecks(){
    auto graph=full();check(!graph.ready(17,23,300),"composition alone is no encoder receipt");
    check(graph.chroma(100,200)&&graph.pack(200,300)&&!graph.ready(17,23,300),"native UV handoff alone cannot certify luma");
    check(graph.luma(100,300)&&graph.ready(17,23,300),"same-frame intermediate, chroma, UV and luma reach the same planar image");
    check(!graph.ready(18,23,300)&&!graph.ready(17,24,300)&&!graph.ready(17,23,301),"same-sized other frame, epoch or encoder resource never borrows coverage");
    check(graph.contains(100)&&graph.contains(200)&&graph.contains(300)&&!graph.contains(400),"mutation tracking covers only this retained image graph");
    graph.invalidate();check(!graph.ready(17,23,300),"unrecognized write permanently revokes the current graph");
    graph=full();check(graph.luma(100,300)&&graph.chroma(100,200)&&graph.pack(200,300)&&graph.ready(17,23,300),"independent luma/chroma edges permit either valid native ordering");
    graph=full();check(graph.directChroma(100,300)&&!graph.ready(17,23,300)&&graph.luma(100,300)&&graph.ready(17,23,300),"direct planar UV and luma reach the exact same retained output");
    graph=full();check(graph.luma(100,300)&&graph.directChroma(100,300)&&graph.ready(17,23,300),"direct planar conversion retains either native plane ordering");
    for(unsigned mode=0;mode<7;++mode){graph=full();bool accepted=true;
        if(mode==0)accepted=graph.directChroma(101,300);
        if(mode==1)accepted=graph.directChroma(100,100);
        if(mode==2){check(graph.luma(100,301),"direct UV foreign-output fixture");accepted=graph.directChroma(100,300);}
        if(mode==3){check(graph.chroma(100,200),"intermediate UV fixture");accepted=graph.directChroma(100,300);}
        if(mode==4){check(graph.directChroma(100,300),"direct UV fixture");accepted=graph.chroma(100,200);}
        if(mode==5){check(graph.directChroma(100,300),"duplicate direct UV fixture");accepted=graph.directChroma(100,300);}
        if(mode==6){check(graph.directChroma(100,300),"wrong luma fixture");accepted=graph.luma(100,301);}
        check(!accepted&&!graph.ready(17,23,300),"direct UV cannot mix resources, conversion paths, duplicate writes or luma outputs");
    }
    for(unsigned mode=0;mode<9;++mode){graph=full();bool accepted=true;
        if(mode==0)accepted=graph.chroma(101,200);
        if(mode==1)accepted=graph.chroma(100,100);
        if(mode==2)accepted=graph.pack(200,300);
        if(mode==3){check(graph.chroma(100,200),"negative fixture chroma");accepted=graph.pack(201,300);}
        if(mode==4){check(graph.chroma(100,200)&&graph.pack(200,300),"negative fixture UV");accepted=graph.luma(100,301);}
        if(mode==5)accepted=graph.draw(1,{0,0,1,1});
        if(mode==6)accepted=graph.image(101);
        if(mode==7)accepted=graph.luma(101,300);
        if(mode==8){check(graph.luma(100,300),"negative fixture luma");accepted=graph.draw(1,{0,0,1,1});}
        check(!accepted&&!graph.complete()&&!graph.ready(17,23,300),"unknown edge, overwrite, replacement or conversion mismatch revokes the whole candidate");
    }
    for(const auto rect:{CompositionRect{0,0,0,4},CompositionRect{63,0,2,4},CompositionRect{0,63,4,2},CompositionRect{0xffffffffu,0,1,1}}){
        NativeComposition bad;check(bad.begin(17,23,64,64,3)&&bad.image(100),"bounded rectangle fixture");check(!bad.draw(1,rect),"empty, outside and wrapping view rectangles are refused");
    }
    NativeComposition holes;check(holes.begin(17,23,64,64,3)&&holes.image(100)&&holes.draw(1,{0,0,32,32})&&holes.draw(2,{32,0,32,32}),"both eyes can still leave missing canvas coverage");
    check(!holes.complete()&&!holes.chroma(100,200),"eye bits without complete non-overlapping image coverage cannot advance");
    NativeComposition invalid;check(!invalid.begin(0,23,64,64,3)&&!invalid.image(100),"missing frame identity is refused");
}
}
int main(){try{shaders();graphChecks();std::printf("native composition/source graph: %u checks PASS\n",checks);return 0;}catch(const std::exception& e){std::fprintf(stderr,"composition failure after %u: %s\n",checks,e.what());return 1;}}
