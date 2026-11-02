// Passive completion-event reader. Never dequeues or requeues camera buffers.
#pragma once
#include "linux_inventory.h"
#include <linux/perf_event.h>
#include <map>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>

namespace room_camera_api {
inline std::uint64_t monotonicNs(){timespec time{};clock_gettime(CLOCK_MONOTONIC,&time);return std::uint64_t(time.tv_sec)*1000000000u+std::uint64_t(time.tv_nsec);}
struct TraceField {unsigned offset=0,size=0;};
struct Completion {unsigned pid=0,minor=0,index=0,sequence=0,flags=0;std::uint64_t capture=0,dequeue=0;};
class CompletionTrace {
    struct Ring {Fd fd;void* memory=MAP_FAILED;std::size_t bytes=0;};
    std::vector<Ring> rings_;std::map<std::string,TraceField> fields_;unsigned id_=0;
    std::uint64_t samples_=0,producerSamples_=0;Json recent_=Json::array();
    static std::uint64_t field(const std::uint8_t* raw,unsigned size,TraceField f){
        if(!f.size||f.size>8||f.offset>size||f.size>size-f.offset)throw std::runtime_error("Camera completion field exceeds record");
        std::uint64_t value=0;std::memcpy(&value,raw+f.offset,f.size);return value;
    }
    static void copy(void* dst,const std::uint8_t* data,std::size_t size,std::uint64_t position,std::size_t bytes){
        if(!size||bytes>size)throw std::runtime_error("Camera trace record exceeds ring");
        const auto offset=std::size_t(position%size),first=std::min(bytes,size-offset);
        std::memcpy(dst,data+offset,first);if(bytes>first)std::memcpy(static_cast<std::uint8_t*>(dst)+first,data,bytes-first);
    }
public:
    void open(){
        std::istringstream format(readText("/sys/kernel/tracing/events/v4l2/v4l2_dqbuf/format",16384));std::string line;
        while(std::getline(format,line)){
            if(line.rfind("ID:",0)==0)id_=unsigned(std::stoul(line.substr(3)));
            const auto name=line.find("field:"),end=line.find(';'),offset=line.find("offset:"),size=line.find("size:");
            if(name==std::string::npos||end==std::string::npos||offset==std::string::npos||size==std::string::npos)continue;
            const auto declaration=line.substr(name+6,end-name-6);const auto start=declaration.find_last_of(" \t*");
            const auto label=declaration.substr(start==std::string::npos?0:start+1);
            fields_[label]={unsigned(std::stoul(line.substr(offset+7))),unsigned(std::stoul(line.substr(size+5)))};
        }
        for(const auto* name:{"common_type","minor","index","sequence","timestamp","flags"})if(!fields_.count(name))throw std::runtime_error("Camera completion layout unavailable");
        if(!id_||fields_.size()>64)throw std::runtime_error("Camera completion identity invalid");
        std::istringstream cpus(readText("/sys/devices/system/cpu/online",1024));std::string range;std::vector<unsigned> online;
        while(std::getline(cpus,range,',')){
            const auto dash=range.find('-');const unsigned first=unsigned(std::stoul(range)),last=dash==std::string::npos?first:unsigned(std::stoul(range.substr(dash+1)));
            if(last<first||last>63)throw std::runtime_error("Camera CPU inventory exceeds budget");
            for(unsigned cpu=first;cpu<=last;++cpu)online.push_back(cpu);
        }
        const auto page=sysconf(_SC_PAGESIZE);if(page<=0||page>65536)throw std::runtime_error("Camera trace page extent invalid");
        for(unsigned cpu:online){
            perf_event_attr attributes{};attributes.size=sizeof(attributes);attributes.type=PERF_TYPE_TRACEPOINT;
            attributes.config=id_;attributes.sample_period=1;attributes.sample_type=PERF_SAMPLE_TID|PERF_SAMPLE_TIME|PERF_SAMPLE_RAW;
            attributes.wakeup_events=1;attributes.use_clockid=1;attributes.clockid=CLOCK_MONOTONIC;attributes.disabled=1;
            Fd descriptor(int(syscall(SYS_perf_event_open,&attributes,-1,int(cpu),-1,PERF_FLAG_FD_CLOEXEC)));
            if(descriptor.get()<0)throw std::runtime_error("Camera completion reader needs temporary perf permissions; errno="+std::to_string(errno));
            const auto bytes=std::size_t(page)*17;void* memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,descriptor.get(),0);
            if(memory==MAP_FAILED)throw std::runtime_error("Camera completion ring unavailable");
            rings_.push_back({std::move(descriptor),memory,bytes});
            if(ioctl(rings_.back().fd.get(),PERF_EVENT_IOC_ENABLE,0))throw std::runtime_error("Camera completion reader could not start");
        }
        if(rings_.empty())throw std::runtime_error("No camera completion readers");
    }
    std::vector<Completion> read(unsigned producer){
        std::vector<pollfd> descriptors;for(const auto& ring:rings_)descriptors.push_back({ring.fd.get(),POLLIN,0});
        if(poll(descriptors.data(),descriptors.size(),50)<0&&errno!=EINTR)throw std::runtime_error("Camera completion poll failed");
        std::vector<Completion> out;
        for(auto& ring:rings_){
            auto* page=static_cast<perf_event_mmap_page*>(ring.memory);const auto offset=page->data_offset,size=page->data_size;
            if(!offset||!size||offset>ring.bytes||size>ring.bytes-offset)throw std::runtime_error("Camera trace ring layout changed");
            const auto head=__atomic_load_n(&page->data_head,__ATOMIC_ACQUIRE);auto tail=page->data_tail;
            if(head<tail||head-tail>size)throw std::runtime_error("Camera completion events were lost");
            const auto* data=static_cast<const std::uint8_t*>(ring.memory)+offset;
            while(tail<head){
                perf_event_header header{};if(head-tail<sizeof(header))throw std::runtime_error("Incomplete camera completion record");
                copy(&header,data,std::size_t(size),tail,sizeof(header));
                if(header.size<sizeof(header)||header.size>4096||header.size>head-tail)throw std::runtime_error("Invalid camera completion extent");
                std::array<std::uint8_t,4096> storage{};copy(storage.data(),data,std::size_t(size),tail,header.size);tail+=header.size;
                if(header.type==PERF_RECORD_LOST||header.type==PERF_RECORD_LOST_SAMPLES)throw std::runtime_error("Camera completion events were dropped");
                if(header.type!=PERF_RECORD_SAMPLE)continue;
                // PERF_SAMPLE_TID, TIME, then RAW; no inferred field offsets.
                if(header.size<sizeof(header)+20)throw std::runtime_error("Camera completion sample is short");
                const auto* bytes=storage.data()+sizeof(header);unsigned pid=0,rawSize=0;std::uint64_t time=0;
                std::memcpy(&pid,bytes,4);std::memcpy(&time,bytes+8,8);std::memcpy(&rawSize,bytes+16,4);
                if(rawSize>header.size-sizeof(header)-20)throw std::runtime_error("Camera completion payload is short");
                const auto* raw=bytes+20;if(field(raw,rawSize,fields_.at("common_type"))!=id_)continue;
                Completion event;event.pid=pid;event.dequeue=time;event.capture=field(raw,rawSize,fields_.at("timestamp"));
                event.minor=unsigned(field(raw,rawSize,fields_.at("minor")));event.index=unsigned(field(raw,rawSize,fields_.at("index")));
                event.sequence=unsigned(field(raw,rawSize,fields_.at("sequence")));event.flags=unsigned(field(raw,rawSize,fields_.at("flags")));
                ++samples_;if(pid==producer)++producerSamples_;
                if(event.minor==0||event.minor==3){
                    recent_.push_back({{"pid",pid},{"minor",event.minor},{"index",event.index},{"sequence",event.sequence},{"capture",event.capture},{"dequeue",event.dequeue},{"flags",event.flags}});
                    if(recent_.size()>8)recent_.erase(recent_.begin());
                }
                if(pid!=producer)continue;
                if(out.size()==2048)throw std::runtime_error("Camera completion batch exceeds budget");
                out.push_back(event);
            }
            __atomic_store_n(&page->data_tail,tail,__ATOMIC_RELEASE);
        }
        std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.dequeue<b.dequeue;});return out;
    }
    Json diagnostics()const{return {{"samples",samples_},{"producer_samples",producerSamples_},{"recent_color_candidates",recent_}};}
    ~CompletionTrace(){for(auto& ring:rings_){ioctl(ring.fd.get(),PERF_EVENT_IOC_DISABLE,0);if(ring.memory!=MAP_FAILED)munmap(ring.memory,ring.bytes);}}
};
} // namespace room_camera_api
