// Own perf readers only. This never writes tracefs, probes or camera state.
#pragma once
#include "trace_reader.h"

namespace room_camera_api {
class PassiveTrace {
public:
    struct Layout {
        unsigned id=0;std::map<std::string,TraceField> fields;
        std::uint64_t value(const std::vector<std::uint8_t>& raw,const std::string& name)const{
            const auto f=fields.at(name);
            if(!f.size||f.size>8||f.offset>raw.size()||f.size>raw.size()-f.offset)throw std::runtime_error("Passive trace field extent invalid");
            std::uint64_t out=0;for(unsigned i=0;i<f.size;++i)out|=std::uint64_t(raw[f.offset+i])<<(8*i);return out;
        }
        std::vector<std::uint8_t> dynamic(const std::vector<std::uint8_t>& raw,const std::string& name)const{
            const auto packed=value(raw,name),offset=packed&65535u,bytes=packed>>16;
            if(offset>raw.size()||bytes>raw.size()-offset)throw std::runtime_error("Passive trace array extent invalid");
            return {raw.begin()+offset,raw.begin()+offset+bytes};
        }
    };
    struct Sample {unsigned pid=0,tid=0;std::uint64_t rawTime=0;Layout const* layout=nullptr;std::vector<std::uint8_t> bytes;};
private:
    struct Ring {Fd fd;void* memory=MAP_FAILED;std::size_t bytes=0;unsigned layout=0;};
    std::vector<Ring> rings_;std::vector<Layout> layouts_;
    static void copy(void* dst,const std::uint8_t* data,std::size_t size,std::uint64_t position,std::size_t bytes){
        if(!size||bytes>size)throw std::runtime_error("Passive trace record exceeds ring");
        const auto offset=std::size_t(position%size),first=std::min(bytes,size-offset);
        std::memcpy(dst,data+offset,first);if(bytes>first)std::memcpy(static_cast<std::uint8_t*>(dst)+first,data,bytes-first);
    }
public:
    void open(const std::vector<std::string>& paths){
        if(!rings_.empty()||paths.empty()||paths.size()>4)throw std::runtime_error("Invalid passive trace inventory");
        const auto page=sysconf(_SC_PAGESIZE);if(page<=0||page>65536)throw std::runtime_error("Passive trace page extent invalid");
        std::istringstream cpus(readText("/sys/devices/system/cpu/online",1024));std::string range;std::vector<unsigned> online;
        while(std::getline(cpus,range,',')){const auto dash=range.find('-');const unsigned first=unsigned(std::stoul(range)),last=dash==std::string::npos?first:unsigned(std::stoul(range.substr(dash+1)));
            if(last<first||last>63)throw std::runtime_error("Passive trace CPU budget exceeded");
            for(unsigned cpu=first;cpu<=last;++cpu)online.push_back(cpu);}
        for(const auto& path:paths){
            if(path.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_/")!=std::string::npos)throw std::runtime_error("Invalid passive event path");
            Layout layout;std::istringstream format(readText("/sys/kernel/tracing/events/"+path+"/format",16384));std::string line;
            while(std::getline(format,line)){
                if(line.rfind("ID:",0)==0)layout.id=unsigned(std::stoul(line.substr(3)));
                const auto name=line.find("field:"),end=line.find(';'),offset=line.find("offset:"),size=line.find("size:");
                if(name==std::string::npos||end==std::string::npos||offset==std::string::npos||size==std::string::npos)continue;
                const auto declaration=line.substr(name+6,end-name-6);const auto start=declaration.find_last_of(" \t*");
                layout.fields[declaration.substr(start==std::string::npos?0:start+1)]={unsigned(std::stoul(line.substr(offset+7))),unsigned(std::stoul(line.substr(size+5)))};
            }
            if(!layout.id||layout.fields.size()>64)throw std::runtime_error("Passive event identity unavailable");
            layouts_.push_back(std::move(layout));const auto index=unsigned(layouts_.size()-1);
            for(unsigned cpu:online){
                perf_event_attr attributes{};attributes.size=sizeof(attributes);attributes.type=PERF_TYPE_TRACEPOINT;attributes.config=layouts_[index].id;
                attributes.sample_period=1;attributes.sample_type=PERF_SAMPLE_TID|PERF_SAMPLE_TIME|PERF_SAMPLE_RAW;attributes.wakeup_events=1;
                attributes.use_clockid=1;attributes.clockid=CLOCK_MONOTONIC_RAW;attributes.disabled=1;
                Fd fd(int(syscall(SYS_perf_event_open,&attributes,-1,int(cpu),-1,PERF_FLAG_FD_CLOEXEC)));
                if(fd.get()<0)throw std::runtime_error("Passive event permission unavailable");
                const auto bytes=std::size_t(page)*17;void* memory=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd.get(),0);
                if(memory==MAP_FAILED)throw std::runtime_error("Passive trace ring unavailable");
                rings_.push_back({std::move(fd),memory,bytes,index});
                if(ioctl(rings_.back().fd.get(),PERF_EVENT_IOC_ENABLE,0))throw std::runtime_error("Passive trace could not start");
            }
        }
    }
    std::vector<Sample> read(unsigned producer,int timeoutMs=25){
        std::vector<pollfd> descriptors;for(const auto& ring:rings_)descriptors.push_back({ring.fd.get(),POLLIN,0});
        if(timeoutMs<0||timeoutMs>25)throw std::runtime_error("Passive trace wait exceeds budget");
        if(poll(descriptors.data(),descriptors.size(),timeoutMs)<0&&errno!=EINTR)throw std::runtime_error("Passive trace poll failed");
        std::vector<Sample> out;
        for(auto& ring:rings_){
            auto* page=static_cast<perf_event_mmap_page*>(ring.memory);const auto offset=page->data_offset,size=page->data_size;
            if(!offset||!size||offset>ring.bytes||size>ring.bytes-offset)throw std::runtime_error("Passive trace ring layout changed");
            const auto head=__atomic_load_n(&page->data_head,__ATOMIC_ACQUIRE);auto tail=page->data_tail;
            if(head<tail||head-tail>size)throw std::runtime_error("Passive trace overflow");
            const auto* data=static_cast<const std::uint8_t*>(ring.memory)+offset;
            while(tail<head){
                perf_event_header header{};if(head-tail<sizeof(header))throw std::runtime_error("Passive trace header is short");
                copy(&header,data,std::size_t(size),tail,sizeof(header));
                if(header.size<sizeof(header)||header.size>4096||header.size>head-tail)throw std::runtime_error("Invalid passive trace record");
                std::array<std::uint8_t,4096> storage{};copy(storage.data(),data,std::size_t(size),tail,header.size);tail+=header.size;
                if(header.type==PERF_RECORD_LOST||header.type==PERF_RECORD_LOST_SAMPLES)throw std::runtime_error("Passive trace lost samples");
                if(header.type!=PERF_RECORD_SAMPLE)continue;
                if(header.size<sizeof(header)+20)throw std::runtime_error("Passive trace sample is short");
                const auto* bytes=storage.data()+sizeof(header);unsigned pid=0,tid=0,count=0;std::uint64_t time=0;
                std::memcpy(&pid,bytes,4);std::memcpy(&tid,bytes+4,4);std::memcpy(&time,bytes+8,8);std::memcpy(&count,bytes+16,4);
                if(count>header.size-sizeof(header)-20)throw std::runtime_error("Passive trace payload is short");
                if(producer&&pid!=producer)continue;
                if(out.size()>=8192)throw std::runtime_error("Passive trace batch budget exceeded");
                out.push_back({pid,tid,time,&layouts_[ring.layout],{bytes+20,bytes+20+count}});
            }
            __atomic_store_n(&page->data_tail,tail,__ATOMIC_RELEASE);
        }
        std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.rawTime<b.rawTime;});return out;
    }
    ~PassiveTrace(){for(auto& ring:rings_){ioctl(ring.fd.get(),PERF_EVENT_IOC_DISABLE,0);if(ring.memory!=MAP_FAILED)munmap(ring.memory,ring.bytes);}}
};
} // namespace room_camera_api
