// Observe completed producer transfers. Never open an I2C device or issue a
// transfer. Observed settings are distinct from per-frame sensor metadata.
#pragma once
#include "passive_trace.h"
#include <deque>
namespace room_camera_api {
class ExposureRegisters {
    struct Transfer {unsigned address=0;std::vector<std::uint8_t> bytes;std::uint64_t started=0;};
    struct Setting {std::uint64_t released=0,earliest=0,stableSince=0;unsigned coarse=0;};
    struct Anchor {
        struct Bound {std::uint64_t earliest;unsigned minimum,maximum;};
        std::uint64_t released=0;std::deque<Bound> history;
        void add(std::uint64_t earliest,unsigned coarse){
            const auto minimum=history.empty()?coarse:std::min(history.back().minimum,coarse);
            const auto maximum=history.empty()?coarse:std::max(history.back().maximum,coarse);
            history.push_back({earliest,minimum,maximum});if(history.size()>128)history.pop_front();
        }
        const Bound* at(std::uint64_t eof)const{
            const Bound* found=nullptr;for(const auto& value:history)if(value.earliest<=eof)found=&value;
            return found;
        }
    };
    struct Eye {bool hold=false;unsigned mask=0,coarse=0;std::deque<Setting> history;std::deque<Anchor> anchors;};
    std::map<unsigned,Transfer> pending_;std::array<Eye,2> eyes_{};
    std::uint64_t successful_=0,failed_=0;
    static std::array<bool,2> affected(unsigned address){return {address==0x1a||address==0x36,address==0x10||address==0x36};}
    void unknown(unsigned address){const auto mask=affected(address);for(unsigned i=0;i<2;++i)if(mask[i])eyes_[i]=Eye{};}
    void completed(const Transfer& transfer,std::uint64_t end){
        if(transfer.bytes.size()!=3)return; // Two bytes select a register for a native read.
        const unsigned reg=unsigned(transfer.bytes[0])*256+transfer.bytes[1],value=transfer.bytes[2];const auto mask=affected(transfer.address);
        ++successful_;
        for(unsigned i=0;i<2;++i)if(mask[i]){
            auto& eye=eyes_[i];
            if(reg==0x0104){
                if(value==1){eye.hold=true;eye.mask=0;eye.coarse=0;}
                else if(value==0&&eye.hold){
                    eye.hold=false;
                    if(eye.mask!=3||!eye.coarse){eye.history.clear();eye.anchors.clear();continue;}
                    const auto stable=eye.history.empty()||eye.history.back().coarse!=eye.coarse?end:eye.history.back().stableSince;
                    for(auto& anchor:eye.anchors)anchor.add(transfer.started,eye.coarse);
                    eye.history.push_back({end,transfer.started,stable,eye.coarse});if(eye.history.size()>128)eye.history.pop_front();
                }else eye=Eye{};
            }else if(reg==0x0202||reg==0x0203){
                if(!eye.hold){eye=Eye{};continue;}
                if(reg==0x0202){eye.coarse=(eye.coarse&255u)|(value<<8);eye.mask|=1;}
                else {eye.coarse=(eye.coarse&65280u)|value;eye.mask|=2;}
            }else if(reg==0x0100||(reg>=0x0300&&reg<=0x034f)||reg==0x0901||reg==0x0220){
                // Clock, mode, dimensions or stream changes invalidate the
                // association. A later complete hold group can rebuild evidence.
                eye=Eye{};
            }
        }
    }
public:
    void observe(unsigned tid,std::uint64_t raw,const PassiveTrace::Layout& layout,const std::vector<std::uint8_t>& bytes){
        if(layout.value(bytes,"adapter_nr")!=0)return;
        if(layout.fields.count("addr")){
            const unsigned address=unsigned(layout.value(bytes,"addr"));const auto mask=affected(address);if(!mask[0]&&!mask[1])return;
            const auto message=layout.dynamic(bytes,"buf");
            if(layout.value(bytes,"msg_nr")!=0||layout.value(bytes,"flags")!=0||message.size()!=layout.value(bytes,"len")||message.size()>32){
                unknown(address);pending_.erase(tid);return;
            }
            if(pending_.size()>=32&&!pending_.count(tid))throw std::runtime_error("Exposure transfer inventory exceeds budget");
            if(const auto old=pending_.find(tid);old!=pending_.end())unknown(old->second.address);
            pending_[tid]={address,message,raw};
        }else{
            const auto found=pending_.find(tid);if(found==pending_.end())return;
            const auto transfer=std::move(found->second);pending_.erase(found);
            if(transfer.bytes.size()==2)return;
            if(layout.value(bytes,"nr_msgs")!=1||layout.value(bytes,"ret")!=1||raw<transfer.started||raw-transfer.started>10000000u){
                ++failed_;unknown(transfer.address);return;
            }
            completed(transfer,raw);
        }
    }
    void read(PassiveTrace& trace,unsigned producer){for(const auto& sample:trace.read(producer,0))observe(sample.tid,sample.rawTime,*sample.layout,sample.bytes);}
    Json settings(const std::string& role,std::uint64_t sof,std::uint64_t eof,std::uint64_t previous=0){
        const int index=role=="passthrough_left"?0:role=="passthrough_right"?1:-1;
        if(index<0||eof<sof)return nullptr;
        for(const auto& item:pending_){const auto mask=affected(item.second.address);
            if(mask[unsigned(index)]&&item.second.started<=eof&&item.second.bytes.size()==3)return nullptr;}
        auto& eye=eyes_[unsigned(index)];const auto& history=eye.history;
        const Setting* selected=nullptr;for(const auto& s:history)if(s.released<sof)selected=&s;
        if(!selected)return nullptr;
        bool unchanged=true;unsigned groups=0;
        for(const auto& s:history)if(s.released>=selected->stableSince){
            if(s.released<=eof)++groups;
            if(s.earliest<=eof&&s.coarse!=selected->coarse)unchanged=false;
        }
        Json result={{"coarse_integration_lines",selected->coarse},{"group_release_raw_ns",selected->released},
            {"group_release_start_raw_ns",selected->earliest},
            {"same_value_since_raw_ns",selected->stableSince},{"same_value_group_count",groups},
            {"unchanged_through_eof",unchanged},{"successful_i2c_result",true},
            {"frame_latch_verified",false}};
        // An earlier settled cohort establishes a known sensor state. During
        // auto-exposure, retain every successfully written value since that
        // anchor through this frame's EOF. A later write cannot affect an
        // earlier image. Include a release transfer that starts before EOF
        // even if its completion is later; its exact latch time is unknown.
        // This envelope bounds the exposure without claiming which
        // group latched to this frame. Unknown/failed/mode-changing writes have
        // already cleared this history; no earlier authority survives them.
        if(previous&&previous<sof&&sof-previous>=14000000u&&sof-previous<=40000000u){
            const auto period=sof-previous;const Setting* anchor=nullptr;
            for(std::size_t i=0;i<history.size();++i){const auto& candidate=history[i];
                if(candidate.released>=sof)break;
                std::size_t first=i;while(first&&history[first-1].stableSince==candidate.stableSince)--first;
                if(i-first>=2&&candidate.released-history[first].released>=2*period)anchor=&candidate;
            }
            if(anchor&&(eye.anchors.empty()||anchor->released>eye.anchors.back().released)){
                Anchor known{anchor->released,{}};
                for(const auto& s:history)if(s.released>=known.released)known.add(s.earliest,s.coarse);
                eye.anchors.push_back(known);while(eye.anchors.size()>8)eye.anchors.pop_front();
            }
            const Anchor* known=nullptr;for(const auto& item:eye.anchors)if(item.released<sof)known=&item;
            const auto* bound=known?known->at(eof):nullptr;
            if(bound&&selected->released<sof&&sof-selected->released<=2*period){
                result["envelope_verified"]=true;result["envelope_anchor_raw_ns"]=known->released;
                result["envelope_min_coarse"]=bound->minimum;result["envelope_max_coarse"]=bound->maximum;
            }
        }
        return result;
    }
    Json diagnostics()const{return {{"successful_register_writes",successful_},{"failed_transfers",failed_}};}
};
} // namespace room_camera_api
