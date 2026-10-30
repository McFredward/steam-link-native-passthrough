// Explicit host-only codec timing. No device probes, installs or live services.
#include "receiver/stream_bridge_mask_hevc_pool.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include "receiver/sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <thread>

namespace h=stream_bridge_mask_hevc;
namespace sb=stream_bridge;
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv){try{
    if(argc!=8){std::fprintf(stderr,"Usage: mask_hevc_benchmark fresh|serial|pool CODEC UTILITY OWNED_FIXTURE WIDTH HEIGHT FRAMES\n");return 2;}
    const std::string mode=argv[1];const h::Libraries libraries{std::filesystem::absolute(argv[2]),std::filesystem::absolute(argv[3])};
    const unsigned width=std::stoul(argv[5]),height=std::stoul(argv[6]),frames=std::stoul(argv[7]);
    if((mode!="fresh"&&mode!="serial"&&mode!="pool")||!frames||frames>1000||width>8192||height>8192)throw std::runtime_error("invalid bounded benchmark arguments");
    std::ifstream source(argv[4],std::ios::binary);if(!source||std::filesystem::file_size(argv[4])>sb::MaximumHevcCoverageBitstreamBytes)throw std::runtime_error("owned bounded input required");
    const std::vector<std::uint8_t> coded{std::istreambuf_iterator<char>(source),{}};std::vector<std::uint8_t> payload;
    if(!sb::wrapHevcCoverage({coded.data(),coded.size()},width,height,payload))throw std::runtime_error("fixture outside independent full-range contract");
    h::Input input{};input.source.connection[0]=1;input.source.generation=1;input.binding[0]=2;
    input.descriptor={1,1,std::uint16_t(width),std::uint16_t(height),1};input.payload={payload.data(),payload.size()};
    h::Decoder decoder;h::Pool pool;
    if(!(mode=="pool"?pool.prepareCold(libraries):decoder.prepareCold(libraries)))throw std::runtime_error("explicit private backend unavailable");
    std::set<std::uint64_t> completed;h::DecodedMask output;h::Pool::Result pooled;const auto started=Clock::now();
    const auto deadline=started+std::chrono::seconds(30);std::uint64_t firstMicros=0;unsigned submitted=0;
    const auto accept=[&](const h::DecodedMask& result){
        if(result.source.connection!=input.source.connection||result.source.generation!=input.source.generation||result.binding!=input.binding
            ||result.descriptor.encoderGeneration!=1||result.descriptor.codedWidth!=width||result.descriptor.codedHeight!=height
            ||!result.descriptor.sourceFrame||result.descriptor.sourceFrame>frames||result.samples.size()!=std::size_t(width)*height||!completed.insert(result.descriptor.sourceFrame).second)
            throw std::runtime_error("wrong/duplicate/incomplete decoded frame");
        if(!firstMicros)firstMicros=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started).count();
    };
    while(completed.size()<frames&&Clock::now()<deadline){
        if(mode=="pool"){
            while(submitted<frames){input.descriptor.sourceFrame=submitted+1;if(!pool.submit(input))break;++submitted;}
            bool progress=false;
            while(pool.take(pooled)){if(pooled.status!=h::Status::Ok)throw std::runtime_error(h::statusName(pooled.status));accept(pooled.decoded);progress=true;}
            if(!progress)std::this_thread::yield();
        }else{
            if(mode=="fresh"){decoder.stopCold();if(!decoder.prepareCold(libraries))throw std::runtime_error("fresh prepare refused");}
            input.descriptor.sourceFrame=++submitted;const auto status=decoder.decodeIntra(input,output);
            if(status!=h::Status::Ok)throw std::runtime_error(h::statusName(status));
            accept(output);
        }
    }
    if(completed.size()!=frames)throw std::runtime_error("bounded decode deadline exceeded");
    const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-started).count();
    const auto stats=mode=="pool"?pool.statistics():decoder.statistics();
    const auto& pixels=mode=="pool"?pooled.decoded.samples:output.samples;
    frame_receiver::Sha256 hash;hash.update(pixels.data(),pixels.size());const auto digest=hash.finish();char hex[65]{};
    for(unsigned i=0;i<digest.size();++i)std::snprintf(hex+2*i,3,"%02x",unsigned(digest[i]));
    std::uint64_t grows=0,reuses=0;
#ifdef SFNP_BENCH_REUSE_COUNTERS
    grows=stats.sampleBufferGrows;reuses=stats.sampleBufferReuses;
#endif
    std::printf("{\"mode\":\"%s\",\"extent\":[%u,%u],\"encoded_bytes\":%zu,\"frames\":%u,\"elapsed_us\":%lld,\"first_result_us\":%llu,\"frames_per_second\":%.3f,\"contexts_opened\":%llu,\"decoder_total_us\":%llu,\"R8_buffer_grows\":%llu,\"R8_buffer_reuses\":%llu,\"last_R8_sha256\":\"%s\",\"identity_errors\":0,\"device_actions\":false}\n",
        mode.c_str(),width,height,coded.size(),frames,static_cast<long long>(elapsed),static_cast<unsigned long long>(firstMicros),double(frames)*1000000/double(elapsed),static_cast<unsigned long long>(stats.contextsOpened),static_cast<unsigned long long>(stats.totalMicros),static_cast<unsigned long long>(grows),static_cast<unsigned long long>(reuses),hex);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}}
