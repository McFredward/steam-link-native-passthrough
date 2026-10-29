#include "pc/stream-bridge-mask-config.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

int main(){
    using stream_bridge::sender::maskHevcBitrate;
    unsigned checks=0;
    const auto check=[&](bool value,const char* why){
        ++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
    };
    check(maskHevcBitrate(std::nullopt)==20000000,"ordinary install without configuration selects HEVC at20 Mbit/s");
    check(maskHevcBitrate("SFNP_MASK_HEVC_V1\n0\n")==0,"explicit lossless preference overrides the default");
    check(maskHevcBitrate("SFNP_MASK_HEVC_V1\n20000000\n")==20000000,"previous explicit HEVC selection is preserved");
    check(maskHevcBitrate("SFNP_MASK_HEVC_V1\n35000000\n")==35000000,"custom bitrate overrides the default");
    check(maskHevcBitrate("SFNP_MASK_HEVC_V1\n1000000\n")==1000000,"minimum supported bitrate");
    check(maskHevcBitrate("SFNP_MASK_HEVC_V1\n200000000\n")==200000000,"maximum supported bitrate");
    for(const auto invalid:{"", "SFNP_MASK_HEVC_V1\n", "SFNP_MASK_HEVC_V1\n999999\n",
        "SFNP_MASK_HEVC_V1\n200000001\n", "SFNP_MASK_HEVC_V1\n4294967296\n", "SFNP_MASK_HEVC_V1\n-20\n",
        "SFNP_MASK_HEVC_V1\n20000000", "SFNP_MASK_HEVC_V1\n20000000\nextra", "unrelated\n20000000\n"}){
        check(maskHevcBitrate(invalid)==0,"present invalid configuration cannot silently enable lossy masks");
    }
    std::printf("%u mask codec default/override checks passed\n",checks);
}
