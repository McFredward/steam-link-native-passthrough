#pragma once
#include "bridge/stream_bridge_hevc_coverage.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace stream_bridge::sender {
// One diagnostic prefix snapshot per provider lifetime. This is never syntax
// admission or frame authority. No allocation, file I/O, mutex or retry loop.
struct MaskHevcSyntaxTrace {
    struct Nal {
        unsigned type=255,bytes=0,copied=0;
        std::array<std::uint8_t,256> prefix{};
    };
    unsigned width=0,height=0,bytes=0,count=0;
    unsigned prefixBytes=0;
    std::array<std::uint8_t,32> prefix{};
    bool more=false;
    std::array<Nal,8> nals{};
};
class MaskHevcSyntaxTraceSlot {
    std::atomic<unsigned> phase_{0}; // empty,writing,ready,consumed
    MaskHevcSyntaxTrace trace_{};
    static bool start(Bytes bytes,std::size_t from,std::size_t& at,unsigned& length)noexcept{
        for(auto i=from;i+2<bytes.size;++i)if(!bytes.data[i]&&!bytes.data[i+1]){
            if(bytes.data[i+2]==1){at=i;length=3;return true;}
            if(i+3<bytes.size&&!bytes.data[i+2]&&bytes.data[i+3]==1){at=i;length=4;return true;}
        }return false;
    }
public:
    bool captureOnce(Bytes bytes,unsigned width,unsigned height)noexcept{
        if(!bytes.data||!bytes.size||bytes.size>MaximumHevcCoverageBitstreamBytes)return false;
        unsigned empty=0;
        if(!phase_.compare_exchange_strong(empty,1,std::memory_order_relaxed))return false;
        trace_.width=width;trace_.height=height;trace_.bytes=unsigned(bytes.size);
        trace_.prefixBytes=std::min(trace_.bytes,32u);
        std::copy_n(bytes.data,trace_.prefixBytes,trace_.prefix.data());
        std::size_t at=0;unsigned length=0;
        if(start(bytes,0,at,length))for(;;){
            const auto body=at+length;std::size_t next=0;unsigned nextLength=0;
            const bool more=start(bytes,body,next,nextLength);const auto end=more?next:bytes.size;
            auto& nal=trace_.nals[trace_.count++];nal.bytes=unsigned(end-body);
            if(nal.bytes>=2)nal.type=(bytes.data[body]>>1)&63;
            // Full ordinary parameter sets/AUD, only32 bytes of any picture or
            // SEI body. Never retain a full image in this header diagnostic.
            const unsigned limit=nal.type>=32&&nal.type<=35?256u:32u;
            nal.copied=std::min(nal.bytes,limit);
            std::copy_n(bytes.data+body,nal.copied,nal.prefix.data());
            if(!more||trace_.count==trace_.nals.size()){trace_.more=more;break;}
            at=next;length=nextLength;
        }
        phase_.store(2,std::memory_order_release);return true;
    }
    bool take(MaskHevcSyntaxTrace& out)noexcept{
        unsigned ready=2;
        if(!phase_.compare_exchange_strong(ready,3,std::memory_order_acquire))return false;
        out=trace_;return true;
    }
};
}
