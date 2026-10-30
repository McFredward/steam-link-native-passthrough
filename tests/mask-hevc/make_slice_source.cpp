// Owned fractional foliage/rain only; no GPU/device/runtime access.
#include "probe_pattern.h"
#include <cstdio>
#include <string>
#include <vector>
int main(int argc,char** argv){
    const bool small=argc==2&&std::string(argv[1])=="small";
    if(!small&&(argc!=2||std::string(argv[1])!="full"))return 2;
    std::vector<std::uint8_t> full,output;
    for(unsigned frame=0;frame<16;++frame){
        sfnp_probe::pattern(frame,full);
        if(small){
            output.assign(256u*256u*3u/2u,128);
            for(unsigned y=0;y<256;++y)
                std::copy_n(full.data()+std::size_t(350+y)*sfnp_probe::Width+450,256,output.data()+std::size_t(y)*256);
        }else output=full;
        // All chroma bytes are neutral128, so the same bytes are planar420.
        if(std::fwrite(output.data(),1,output.size(),stdout)!=output.size())return 1;
    }
    return 0;
}
