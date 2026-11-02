// Prepare bounded metric Gaussian surface patches outside the XR frame loop.
#pragma once
#include "native_scene_mesh.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace room_native {
struct GaussianRecord {std::uint32_t id;float v[11];};
static_assert(sizeof(GaussianRecord)==48);
class GaussianScene {
    std::unordered_map<std::uint32_t,double> seen_;
    std::string path_;
public:
    void append(SceneMeshBuilder& builder,const nlohmann::json& state,double now){
        if(!state.contains("gaussian_file")||state.at("gaussian_file").is_null())return;
        const auto path=state.at("gaussian_file").get<std::string>();
        if(path!=path_){seen_.clear();path_=path;}
        if(path.size()>4096||path.find("/.pc-gaussians-")==std::string::npos)throw std::runtime_error("Invalid Gaussian model path");
        const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        struct File {int fd;~File(){if(fd>=0)::close(fd);}} owned{fd};
        if(fd<0&&errno==ENOENT)return;
        struct stat info{};if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_uid!=getuid()||(info.st_mode&0077)
            ||info.st_size<=0||info.st_size>80000*48||info.st_size%48)throw std::runtime_error("Untrusted Gaussian model");
        std::vector<GaussianRecord> records(std::size_t(info.st_size)/48);std::size_t offset=0;
        while(offset<std::size_t(info.st_size)){const auto n=read(fd,reinterpret_cast<char*>(records.data())+offset,std::size_t(info.st_size)-offset);
            if(n<0&&errno==EINTR)continue;
            if(n<=0)throw std::runtime_error("Truncated Gaussian model");
            offset+=std::size_t(n);}
        std::unordered_map<std::uint32_t,double> current;current.reserve(records.size());
        for(const auto& record:records){
            if(record.id>=80000||current.count(record.id))throw std::runtime_error("Invalid Gaussian identity");
            for(float v:record.v)if(!std::isfinite(v)||std::abs(v)>1000)throw std::runtime_error("Invalid Gaussian value");
            const V3 p{record.v[0],record.v[1],record.v[2]},u{record.v[3],record.v[4],record.v[5]},v{record.v[6],record.v[7],record.v[8]};
            const auto a=length(u),b=length(v);const float opacity=record.v[9],quality=record.v[10];
            if(a<.001f||a>.15f||b<.001f||b>.15f||std::abs(dot(u,v))>a*b*.02f||opacity<=0||opacity>1||(quality!=1&&quality!=2))
                throw std::runtime_error("Invalid Gaussian covariance");
            const auto found=seen_.find(record.id);const float first=float(found==seen_.end()?now:found->second);current.emplace(record.id,first);
            if(builder.vertices.size()+6>SceneVertexLimit)throw std::runtime_error("Gaussian scene exceeds vertex budget");
            const std::array<std::array<float,2>,6> uv{{{-3,-3},{3,-3},{3,3},{-3,-3},{3,3},{-3,3}}};
            for(const auto& xy:uv){const auto point=p+u*xy[0]+v*xy[1];
                builder.vertices.push_back({{point.x,point.y,point.z},{.12f,.88f,.78f,opacity},{xy[0],xy[1],quality},first,5});}
        }
        seen_=std::move(current);
    }
};
} // namespace room_native
