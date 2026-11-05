// Parse and prepare immutable geometry away from the XR frame loop.
#pragma once
#include "native_scene_mesh.h"
#include "native_gaussian.h"
#include "native_surface_preview.h"
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace room_native {
struct PreparedScene {
    std::shared_ptr<const nlohmann::json> state;
    std::shared_ptr<const std::vector<SceneVertex>> vertices;
    bool allowed=false;
};
class SceneSnapshots {
    std::atomic<bool> stop_{false};std::shared_ptr<const PreparedScene> latest_;std::thread reader_;
    std::shared_ptr<const Mat> focus_;
    std::size_t drawBudget_=SceneVertexLimit;
    std::chrono::steady_clock::time_point start_;
    void readLoop(){
        std::string pending;std::array<char,16384> bytes{};
        MeshView mesh;GaussianScene gaussians;SurfaceScene surfaces;SurfaceTiles tiles(drawBudget_);
        std::shared_ptr<const std::vector<SceneVertex>> vertices;
        std::shared_ptr<const nlohmann::json> lastValue;
        std::shared_ptr<const Mat> lastFocus;
        const auto prepare=[&](std::shared_ptr<const nlohmann::json> value,bool force){
            const auto revision=mesh.revision;const bool allowed=mesh.allowed;
            const auto now=std::chrono::duration<double>(std::chrono::steady_clock::now()-start_).count();
            mesh.update(*value,now);const auto focus=std::atomic_load(&focus_);
            if(force||!vertices||revision!=mesh.revision||allowed!=mesh.allowed||(mesh.allowed&&tiles.limited&&focus!=lastFocus)){
                SceneMeshBuilder builder;builder.update(mesh);
                if(mesh.allowed){
                    if(value->contains("surface_tiles")&&!value->at("surface_tiles").empty())tiles.append(builder,*value,now,focus.get());
                    else if(value->contains("surface_file")&&!value->at("surface_file").is_null())surfaces.append(builder,*value,now);
                    else gaussians.append(builder,*value,now);}
                vertices=std::make_shared<const std::vector<SceneVertex>>(std::move(builder.vertices));
            }
            lastValue=value;lastFocus=focus;
            std::shared_ptr<const PreparedScene> result=std::make_shared<const PreparedScene>(PreparedScene{std::move(value),vertices,mesh.allowed});
            std::atomic_store(&latest_,std::move(result));
        };
        try{while(!stop_){
            pollfd descriptor{STDIN_FILENO,POLLIN,0};const auto ready=poll(&descriptor,1,50);
            if(ready<0&&errno==EINTR)continue;
            if(ready<0)break;
            if(!ready){if(lastValue&&mesh.allowed&&tiles.limited&&std::atomic_load(&focus_)!=lastFocus)prepare(lastValue,true);continue;}
            const auto size=read(STDIN_FILENO,bytes.data(),bytes.size());
            if(size<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))continue;
            if(size<=0)break;
            pending.append(bytes.data(),std::size_t(size));if(pending.size()>16*1024*1024)throw std::runtime_error("Room scene snapshot exceeds budget");
            for(;;){const auto end=pending.find('\n');if(end==std::string::npos)break;
                auto value=std::make_shared<const nlohmann::json>(nlohmann::json::parse(pending.begin(),pending.begin()+std::ptrdiff_t(end)));
                if(value->value("schema","")!="sfnp-room-dashboard-v1"||!value->contains("room")||!value->contains("world_revision")
                    ||!value->contains("corners")||value->at("corners").size()>64)throw std::runtime_error("Invalid room scene snapshot");
                const auto& room=value->at("room");if(!room.is_null()){
                    if(!room.contains("elements")||room.at("elements").size()>256)throw std::runtime_error("Room scene element budget reached");
                    const auto boundedElement=[](const nlohmann::json& element){
                        if(element.at("geometry").at("boundary_xy").size()>64)throw std::runtime_error("Room scene boundary budget reached");};
                    for(const auto& element:room.at("elements"))boundedElement(element);
                    if(!value->at("pending").is_null())boundedElement(value->at("pending"));
                }
                prepare(std::move(value),false);pending.erase(0,end+1);
            }
        }}catch(const std::exception&){failed=true;}
        alive=false;
    }
public:
    std::atomic<bool> alive{true},failed{false};
    explicit SceneSnapshots(std::chrono::steady_clock::time_point start,std::size_t budget=SceneVertexLimit):drawBudget_(budget),start_(start){
        fcntl(STDIN_FILENO,F_SETFL,fcntl(STDIN_FILENO,F_GETFL)|O_NONBLOCK);reader_=std::thread([this]{readLoop();});}
    std::shared_ptr<const PreparedScene> latest()const{return std::atomic_load(&latest_);}
    void focus(const Mat& pose){
        if(!rigid(pose))return;
        const auto old=std::atomic_load(&focus_);
        if(old&&length(position(*old)-position(pose))<.12f&&dot(column(*old,2),column(pose,2))>.9928f)return;
        std::atomic_store(&focus_,std::make_shared<const Mat>(pose));
    }
    ~SceneSnapshots(){stop_=true;if(reader_.joinable())reader_.join();}
};
} // namespace room_native
