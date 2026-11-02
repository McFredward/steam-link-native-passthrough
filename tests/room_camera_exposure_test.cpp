#include "room_scan/camera_api/exposure_registers.h"
#include <cstdio>
using namespace room_camera_api;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void put(std::vector<std::uint8_t>& out,unsigned offset,unsigned value,unsigned bytes){for(unsigned i=0;i<bytes;++i)out.at(offset+i)=std::uint8_t(value>>(8*i));}
const PassiveTrace::Layout writeLayout{1,{{"adapter_nr",{8,4}},{"msg_nr",{12,2}},{"addr",{14,2}},{"flags",{16,2}},{"len",{18,2}},{"buf",{20,4}}}};
const PassiveTrace::Layout resultLayout{2,{{"adapter_nr",{8,4}},{"nr_msgs",{12,2}},{"ret",{14,2}}}};
void transfer(ExposureRegisters& observer,unsigned address,unsigned reg,unsigned value,std::uint64_t time,bool success=true){
    std::vector<std::uint8_t> request(28,0),result(16,0);put(request,14,address,2);put(request,18,3,2);put(request,20,(3u<<16)|24u,4);
    request[24]=std::uint8_t(reg>>8);request[25]=std::uint8_t(reg);request[26]=std::uint8_t(value);
    observer.observe(100,time,writeLayout,request);put(result,12,1,2);put(result,14,success?1:65531,2);
    observer.observe(100,time+1000,resultLayout,result);
}
void group(ExposureRegisters& observer,unsigned coarse,std::uint64_t time){
    transfer(observer,0x36,0x0104,1,time);transfer(observer,0x36,0x0202,coarse>>8,time+10000);
    transfer(observer,0x36,0x0203,coarse&255,time+20000);transfer(observer,0x36,0x0104,0,time+30000);
}
}
int main(){try{
    ExposureRegisters observer;constexpr std::uint64_t sof=1000000000ULL,eof=sof+11000000u;
    check(observer.settings("passthrough_left",sof,eof).is_null(),"unknown native settings remain unknown");
    group(observer,1194,sof-50000000u);group(observer,1194,sof-33000000u);group(observer,1194,sof-16000000u);
    const auto left=observer.settings("passthrough_left",sof,eof),right=observer.settings("passthrough_right",sof,eof);
    check(left==right&&left.at("coarse_integration_lines")==1194&&left.at("same_value_group_count")==3,"successful native broadcast groups bind both sensors");
    check(left.at("unchanged_through_eof")==true&&left.at("frame_latch_verified")==false,"observed registers cannot invent per-frame latch evidence");
    group(observer,1200,sof+1000000u);
    check(observer.settings("passthrough_left",sof,eof).at("unchanged_through_eof")==false,"an exposure change during capture is explicit");
    transfer(observer,0x1a,0x0104,1,sof+2000000u);transfer(observer,0x1a,0x0202,4,sof+3000000u,false);
    check(observer.settings("passthrough_left",sof,eof).is_null()&&!observer.settings("passthrough_right",sof,eof).is_null(),"failed transfer clears only affected sensor evidence");
    transfer(observer,0x10,0x0340,14,sof+4000000u);
    check(observer.settings("passthrough_right",sof,eof).is_null(),"sensor mode changes invalidate preceding exposure state");
    std::vector<std::uint8_t> malformed(24,0);put(malformed,20,(100u<<16)|20u,4);bool refused=false;
    try{writeLayout.dynamic(malformed,"buf");}catch(const std::exception&){refused=true;}
    check(refused,"kernel dynamic payload bounds are validated independently of an exposure provider");
    std::puts("PASS passive successful transfers, dual eye groups, exposure changes, failure isolation and payload bounds");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
