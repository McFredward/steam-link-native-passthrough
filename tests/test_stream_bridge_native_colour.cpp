// Independently owned numeric SPIR-V transfer fixtures; no native bodies.
#include "layer/stream_bridge_native_colour.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
unsigned checks=0;
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
std::string sha(const void* p,std::size_t n){frame_receiver::Sha256 h;h.update(p,n);const auto v=h.finish();
    std::string s;const char* a="0123456789abcdef";for(auto x:v){s+=a[x>>4];s+=a[x&15];}return s;}
void run(){
    using namespace stream_bridge_native_colour;
    const std::array<std::uint32_t,12> keyed{{0x07230203,0x10000,0,8,0,3u<<16|22u,2,32,4u<<16|43u,2,3,19}};
    auto colour=keyed;colour.back()=23;
    const std::array<std::uint32_t,2> ids{{2,3}};
    const auto a=sha(keyed.data(),sizeof keyed),b=sha(colour.data(),sizeof colour);
    const Program from{{keyed.data(),ids.data(),ids.size()},sizeof keyed,a.c_str()};
    const Program to{{colour.data(),ids.data(),ids.size()},sizeof colour,b.c_str()};
    std::array<std::uint32_t,16> out{};std::size_t used=99;
    check(copyAttestedAlternative(keyed.data(),sizeof keyed,from,to,colour.data(),sizeof colour,out.data(),out.size(),used)&&
        used==colour.size()&&!std::memcmp(out.data(),colour.data(),sizeof colour),
        "reviewed keyed family selects exactly the independently attested unkeyed bytes");
    auto renamed=keyed;renamed[2]=991;renamed[3]=32;renamed[6]=19;renamed[9]=19;renamed[10]=20;
    check(copyAttestedAlternative(renamed.data(),sizeof renamed,from,to,colour.data(),sizeof colour,out.data(),out.size(),used),
        "compiler generator/result-ID changes do not require another version pin");
    auto wrong=keyed;wrong.back()++;
    check(!copyAttestedAlternative(wrong.data(),sizeof wrong,from,to,colour.data(),sizeof colour,out.data(),out.size(),used)&&!used,
        "changed arithmetic literal cannot select a colour replacement");
    check(!copyAttestedAlternative(keyed.data(),sizeof keyed,from,to,keyed.data(),sizeof keyed,out.data(),out.size(),used)&&!used,
        "wrong cached alternative never inherits the keyed program's attestation");
    check(!copyAttestedAlternative(keyed.data(),sizeof keyed,from,to,nullptr,0,out.data(),out.size(),used)&&!used,
        "absent current-runtime unkeyed code remains refused");
    check(!copyAttestedAlternative(keyed.data(),sizeof keyed,from,to,colour.data(),sizeof colour,out.data(),2,used)&&!used,
        "short output storage is refused before writing");
    check(!copyAttestedAlternative(keyed.data(),sizeof keyed-1,from,to,colour.data(),sizeof colour,out.data(),out.size(),used)&&!used,
        "truncated input cannot select a colour replacement");
    check(!prepareColourCold(keyed.data(),sizeof keyed)&&!colourReady.load(),
        "an owned toy or matching interface cannot populate the real native colour cache");
    check(!copyUnkeyed(keyed.data(),sizeof keyed,out.data(),out.size(),used)&&!used,
        "production remains inert without independently reviewed current-runtime code");
}
}
int main(){try{run();std::printf("native colour family selection: %u checks PASS; owned numeric fixtures only\n",checks);return 0;}
catch(const std::exception& e){std::fprintf(stderr,"native colour selection failed: %s\n",e.what());return 1;}}
