// One coherent, private binary measured-surface snapshot; no JSON mesh on HUD updates.
#pragma once
#include "native_scene_mesh.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
namespace room_native {
struct SurfaceVertex {std::uint32_t id;float p[3];std::uint8_t quality,padding[3];};
static_assert(sizeof(SurfaceVertex)==20);
class SurfaceScene {
    struct Seen {double first;V3 center;};
    std::unordered_map<std::uint64_t,Seen> seen_;std::string path_;
    std::unordered_map<std::uint64_t,double> covered_;
    static std::uint64_t cell(V3 p){
        const auto index=[](float v){return std::uint64_t(int(std::floor(v/.2f))+8192);};
        return (index(p.x)<<28)|(index(p.y)<<14)|index(p.z);
    }
public:
    void append(SceneMeshBuilder& builder,const nlohmann::json& state,double now){
        const auto path=state.at("surface_file").get<std::string>();
        if(path.size()>4096||path.find("/.pc-surfaces-")==std::string::npos)throw std::runtime_error("Invalid room surface path");
        if(path!=path_){seen_.clear();covered_.clear();path_=path;}
        const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        struct File{int fd;~File(){if(fd>=0)close(fd);}} file{fd};
        if(fd<0&&errno==ENOENT)return;
        struct stat info{};if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||(info.st_mode&0077)
            ||info.st_size<16||info.st_size>16+100000*20+200000*12)throw std::runtime_error("Untrusted room surface snapshot");
        std::vector<std::uint8_t> raw(std::size_t(info.st_size));std::size_t offset=0;
        while(offset<raw.size()){const auto n=read(fd,raw.data()+offset,raw.size()-offset);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("Incomplete room surface snapshot");
            offset+=std::size_t(n);}
        if(std::memcmp(raw.data(),"SFNPMR2\0",8))throw std::runtime_error("Unknown room surface snapshot");
        std::uint32_t count=0,faces=0;std::memcpy(&count,raw.data()+8,4);std::memcpy(&faces,raw.data()+12,4);
        if(count>100000||faces>200000||raw.size()!=16+std::size_t(count)*20+std::size_t(faces)*12)
            throw std::runtime_error("Room surface extent exceeds budget");
        std::vector<SurfaceVertex> vertices(count);if(count)std::memcpy(vertices.data(),raw.data()+16,std::size_t(count)*20);
        std::unordered_set<std::uint32_t> ids;
        for(const auto& v:vertices){if(v.id>=320000||!ids.insert(v.id).second||v.quality>2)throw std::runtime_error("Invalid room surface identity");
            for(float p:v.p)if(!std::isfinite(p)||std::abs(p)>1000)throw std::runtime_error("Invalid room surface position");}
        std::unordered_map<std::uint64_t,Seen> current;current.reserve(faces);
        for(unsigned i=0;i<faces;++i){std::array<std::uint32_t,3> t{};std::memcpy(t.data(),raw.data()+16+std::size_t(count)*20+i*12,12);
            if(t[0]>=count||t[1]>=count||t[2]>=count||t[0]==t[1]||t[1]==t[2]||t[2]==t[0])throw std::runtime_error("Invalid room surface triangle");
            const auto& a=vertices[t[0]];const auto& b=vertices[t[1]];const auto& c=vertices[t[2]];
            const V3 p{a.p[0],a.p[1],a.p[2]},q{b.p[0],b.p[1],b.p[2]},r{c.p[0],c.p[1],c.p[2]},center=(p+q+r)*(1.f/3);
            if(length(cross(q-p,r-p))<=1e-7f)throw std::runtime_error("Degenerate room surface triangle");
            std::array<std::uint32_t,3> keyIds{a.id,b.id,c.id};std::sort(keyIds.begin(),keyIds.end());
            const std::uint64_t key=(std::uint64_t(keyIds[0])<<40)|(std::uint64_t(keyIds[1])<<20)|keyIds[2];
            const auto old=seen_.find(key);double first=old==seen_.end()||length(old->second.center-center)>.04f?now:old->second.first;
            // Retriangulating an already observed area is refinement, not a
            // reason to repeatedly erase it and replay its initial reveal.
            const auto spatial=cell(center);const auto covered=covered_.find(spatial);
            if(covered!=covered_.end()){first=std::min(first,covered->second);covered->second=first;}
            else if(covered_.size()<200000)covered_.emplace(spatial,first);
            current.emplace(key,Seen{first,center});builder.measuredSurface(p,q,r,a.quality==2&&b.quality==2&&c.quality==2,float(first));
        }
        seen_=std::move(current);
    }
};
} // namespace room_native
