// One coherent, private binary measured-surface snapshot; no JSON mesh on HUD updates.
#pragma once
#include "native_scene_mesh.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_set>
#include <memory>
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
    void forgetTriangles(){seen_.clear();}
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

// Dense regions stay separate. Read and prepare only whole nearby regions;
// acquisition and the detailed PC map are independent of the GPU draw budget.
class SurfaceTiles {
    std::size_t budget_;
    struct Entry {SurfaceScene scene;ino_t inode=0;off_t size=0;timespec modified{};
        std::shared_ptr<const std::vector<SceneVertex>> vertices;};
    std::unordered_map<std::string,Entry> entries_;
public:
    explicit SurfaceTiles(std::size_t budget=SceneVertexLimit):budget_(std::min(budget,SceneVertexLimit)){}
    bool limited=false;
    void append(SceneMeshBuilder& builder,const nlohmann::json& state,double now,const Mat* focus=nullptr){
        const auto& tiles=state.at("surface_tiles");
        if(!tiles.is_array()||tiles.size()>8192)throw std::runtime_error("Invalid room region extent");
        struct Tile {std::string path;V3 center;unsigned faces;float score;};
        std::vector<Tile> order;std::unordered_set<std::string> paths;std::size_t total=builder.vertices.size();
        for(const auto& tile:tiles){const auto path=tile.at("path").get<std::string>();const auto& p=tile.at("center");
            const auto faces=tile.at("faces").get<unsigned>();
            if(path.size()>4096||path.find("/.pc-surfaces-")==std::string::npos||!paths.insert(path).second
                ||!p.is_array()||p.size()!=3||faces>200000)throw std::runtime_error("Invalid room region");
            V3 center{p[0].get<float>(),p[1].get<float>(),p[2].get<float>()};
            for(float v:{center.x,center.y,center.z})if(!std::isfinite(v)||std::abs(v)>1002)throw std::runtime_error("Invalid room region center");
            const V3 delta=focus?center-position(*focus):center;
            const bool behind=focus&&dot(delta,column(*focus,2)*-1)<-.5f;
            order.push_back({path,center,faces,length(delta)+(behind?1000.f:0.f)});total+=std::size_t(faces)*3;
        }
        limited=total>budget_;
        std::stable_sort(order.begin(),order.end(),[](const Tile& a,const Tile& b){return a.score<b.score;});
        for(auto it=entries_.begin();it!=entries_.end();)if(!paths.count(it->first))it=entries_.erase(it);else ++it;
        std::unordered_set<std::string> selected;
        for(const auto& tile:order){
            if(builder.vertices.size()+std::size_t(tile.faces)*3>budget_)continue;
            selected.insert(tile.path);auto& entry=entries_[tile.path];struct stat info{};
            if(lstat(tile.path.c_str(),&info)){if(errno==ENOENT)continue;throw std::runtime_error("Room region is unavailable");}
            if(!S_ISREG(info.st_mode)||info.st_uid!=getuid()||(info.st_mode&0077))throw std::runtime_error("Untrusted room region");
            if(!entry.vertices||entry.inode!=info.st_ino||entry.size!=info.st_size
                ||entry.modified.tv_sec!=info.st_mtim.tv_sec||entry.modified.tv_nsec!=info.st_mtim.tv_nsec){
                SceneMeshBuilder region;entry.scene.append(region,{{"surface_file",tile.path}},now);
                if(region.vertices.size()!=std::size_t(tile.faces)*3)throw std::runtime_error("Room region connectivity changed");
                entry.vertices=std::make_shared<const std::vector<SceneVertex>>(std::move(region.vertices));
                entry.inode=info.st_ino;entry.size=info.st_size;entry.modified=info.st_mtim;
            }
            builder.vertices.insert(builder.vertices.end(),entry.vertices->begin(),entry.vertices->end());
        }
        // Files remain owned and complete on the Frame; their prepared GPU
        // copies can be loaded again on a turn without another camera image.
        if(limited)for(auto& item:entries_)if(!selected.count(item.first)){
            item.second.vertices.reset();item.second.scene.forgetTriangles();}
    }
};
} // namespace room_native
