// Passive XRService/media inventory. No stream ownership or device settings.
// Media traversal follows Frametop's discovery (2026 DeeJanuz), based on
// FrameEyeCameraFeed (2026 Curtis English), MIT; see licenses/NativeCamera.txt.
#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <cstring>
#include <sstream>
#include <set>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/syscall.h>
#include <poll.h>
#include <unistd.h>
#include <linux/media.h>
#include <linux/videodev2.h>

namespace room_camera_api {
using Json=nlohmann::json;
class Fd {
    int value_=-1;
public:
    explicit Fd(int value=-1):value_(value){}
    ~Fd(){if(value_>=0)close(value_);}
    Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;
    Fd(Fd&& other)noexcept:value_(std::exchange(other.value_,-1)){}
    Fd& operator=(Fd&& other)noexcept{if(value_>=0)close(value_);value_=std::exchange(other.value_,-1);return *this;}
    int get()const{return value_;}
};
inline std::string readText(const std::string& path,std::size_t limit=65536){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Camera metadata cannot be read: "+path);
    std::string text(limit+1,'\0');input.read(text.data(),std::streamsize(text.size()));text.resize(std::size_t(input.gcount()));
    if(text.size()>limit)throw std::runtime_error("Camera metadata exceeds budget");
    return text;
}
inline std::string linkTarget(const std::string& path){
    std::array<char,4096> text{};const auto n=readlink(path.c_str(),text.data(),text.size());
    return n<=0||n==ssize_t(text.size())?std::string{}:std::string(text.data(),std::size_t(n));
}
inline std::vector<unsigned> numericEntries(const std::string& path,unsigned maximum){
    std::vector<unsigned> out;DIR* directory=opendir(path.c_str());if(!directory)return out;
    while(auto* entry=readdir(directory)){
        const std::string name=entry->d_name;if(name.empty()||name.size()>9||name.find_first_not_of("0123456789")!=std::string::npos)continue;
        if(out.size()==maximum){closedir(directory);throw std::runtime_error("Camera process inventory exceeds budget");}
        out.push_back(unsigned(std::stoul(name)));
    }
    closedir(directory);std::sort(out.begin(),out.end());return out;
}
struct Producer {unsigned pid=0,uid=0;std::string start;};
inline Producer findProducer(){
    Producer producer;
    for(unsigned pid:numericEntries("/proc",32768)){
        const std::string root="/proc/"+std::to_string(pid);
        if(linkTarget(root+"/exe")!="/opt/steamvr/drivers/cv/bin/linuxarm64/XRService")continue;
        struct stat info{};if(stat(root.c_str(),&info))continue;
        if(producer.pid)throw std::runtime_error("Multiple XRService producers are ambiguous");
        const auto status=readText(root+"/stat",8192);const auto end=status.rfind(')');
        if(end==std::string::npos)throw std::runtime_error("Invalid XRService process metadata");
        std::istringstream fields(status.substr(end+1));std::string start;
        for(unsigned index=0;index<20;++index)if(!(fields>>start))throw std::runtime_error("Incomplete XRService process metadata");
        producer={pid,info.st_uid,start};
    }
    if(!producer.pid)throw std::runtime_error("XRService is not running");
    return producer;
}
class MediaGraph {
    struct Topology {
        std::vector<media_v2_entity> entities;std::vector<media_v2_interface> interfaces;
        std::vector<media_v2_pad> pads;std::vector<media_v2_link> links;
    };
    std::vector<Topology> graphs_;
public:
    MediaGraph(const Producer& producer,int pidfd){
        unsigned count=0;
        for(unsigned source:numericEntries("/proc/"+std::to_string(producer.pid)+"/fd",8192)){
            const auto path=linkTarget("/proc/"+std::to_string(producer.pid)+"/fd/"+std::to_string(source));
            if(path.rfind("/dev/media",0)!=0)continue;
            if(++count>8)throw std::runtime_error("Camera media descriptor count exceeds budget");
            Fd fd(int(syscall(SYS_pidfd_getfd,pidfd,source,0)));if(fd.get()<0)continue;
            media_v2_topology counts{};if(ioctl(fd.get(),MEDIA_IOC_G_TOPOLOGY,&counts))continue;
            if(counts.num_entities>8192||counts.num_interfaces>8192||counts.num_pads>32768||counts.num_links>32768)
                throw std::runtime_error("Camera media graph exceeds budget");
            Topology graph;graph.entities.resize(counts.num_entities);graph.interfaces.resize(counts.num_interfaces);
            graph.pads.resize(counts.num_pads);graph.links.resize(counts.num_links);auto data=counts;
            data.ptr_entities=reinterpret_cast<std::uintptr_t>(graph.entities.data());data.ptr_interfaces=reinterpret_cast<std::uintptr_t>(graph.interfaces.data());
            data.ptr_pads=reinterpret_cast<std::uintptr_t>(graph.pads.data());data.ptr_links=reinterpret_cast<std::uintptr_t>(graph.links.data());
            if(ioctl(fd.get(),MEDIA_IOC_G_TOPOLOGY,&data)||data.topology_version!=counts.topology_version
                ||data.num_entities!=counts.num_entities||data.num_interfaces!=counts.num_interfaces
                ||data.num_pads!=counts.num_pads||data.num_links!=counts.num_links)continue;
            graphs_.push_back(std::move(graph));
        }
    }
    std::string sensor(dev_t device)const{
        for(const auto& graph:graphs_){
            unsigned interface=0,entity=0;int exitPad=-1;
            for(const auto& value:graph.interfaces)if(value.devnode.major==major(device)&&value.devnode.minor==minor(device))interface=value.id;
            for(const auto& link:graph.links)if(interface&&link.source_id==interface&&(link.flags&MEDIA_LNK_FL_LINK_TYPE)==MEDIA_LNK_FL_INTERFACE_LINK)entity=link.sink_id;
            std::set<unsigned> visited;
            for(unsigned hop=0;entity&&hop<32&&visited.insert(entity).second;++hop){
                const auto found=std::find_if(graph.entities.begin(),graph.entities.end(),[&](const auto& value){return value.id==entity;});
                if(found==graph.entities.end())break;
                if(found->function==MEDIA_ENT_F_CAM_SENSOR)return std::string(found->name,strnlen(found->name,sizeof(found->name)));
                std::vector<unsigned> sinks;unsigned paired=0;
                for(const auto& pad:graph.pads)if(pad.entity_id==entity&&(pad.flags&MEDIA_PAD_FL_SINK)){
                    sinks.push_back(pad.id);if(exitPad>0&&pad.index==unsigned(exitPad-1))paired=pad.id;
                }
                const unsigned sink=sinks.size()==1?sinks.front():paired;if(!sink)break;
                std::vector<unsigned> sources;
                for(const auto& link:graph.links)if(link.sink_id==sink&&(link.flags&MEDIA_LNK_FL_ENABLED)
                    &&(link.flags&MEDIA_LNK_FL_LINK_TYPE)==MEDIA_LNK_FL_DATA_LINK)sources.push_back(link.source_id);
                if(sources.size()!=1)break;
                const auto pad=std::find_if(graph.pads.begin(),graph.pads.end(),[&](const auto& value){return value.id==sources.front();});
                if(pad==graph.pads.end())break;
                entity=pad->entity_id;exitPad=int(pad->index);
            }
        }
        return {};
    }
};
struct Buffer {unsigned index=0;int ownerFd=-1;std::uint64_t length=0,inode=0;};
struct Camera {
    std::string path,sensor;unsigned minor=0,width=0,height=0,format=0,planes=0,pitch=0;
    std::array<unsigned,VIDEO_MAX_PLANES> planeBytes{};unsigned type=0,producerNodeFd=0;int error=0;
    std::vector<Buffer> buffers;
};
inline Camera inspectCamera(const std::string& path,unsigned source,int pidfd,const Producer& producer,const MediaGraph& media){
    Camera camera;camera.path=path;camera.producerNodeFd=source;Fd fd(int(syscall(SYS_pidfd_getfd,pidfd,source,0)));
    if(fd.get()<0){camera.error=errno;return camera;}
    struct stat info{};if(fstat(fd.get(),&info)||!S_ISCHR(info.st_mode)){camera.error=EINVAL;return camera;}
    camera.minor=minor(info.st_rdev);camera.sensor=media.sensor(info.st_rdev);
    v4l2_format format{};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if(ioctl(fd.get(),VIDIOC_G_FMT,&format)){
        format={};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if(ioctl(fd.get(),VIDIOC_G_FMT,&format)){camera.error=errno;return camera;}
        const auto& value=format.fmt.pix;camera.width=value.width;camera.height=value.height;
        camera.format=value.pixelformat;camera.planes=1;camera.pitch=value.bytesperline;camera.planeBytes[0]=value.sizeimage;
    }else{
        const auto& value=format.fmt.pix_mp;camera.width=value.width;camera.height=value.height;
        camera.format=value.pixelformat;camera.planes=value.num_planes;
        if(!camera.planes||camera.planes>VIDEO_MAX_PLANES){camera.error=EOVERFLOW;return camera;}
        camera.pitch=value.plane_fmt[0].bytesperline;
        for(unsigned p=0;p<camera.planes;++p)camera.planeBytes[p]=value.plane_fmt[p].sizeimage;
    }
    camera.type=format.type;
    for(unsigned index=0;index<64;++index){
        v4l2_buffer buffer{};std::array<v4l2_plane,VIDEO_MAX_PLANES> planes{};
        buffer.index=index;buffer.type=camera.type;buffer.memory=V4L2_MEMORY_DMABUF;
        if(camera.type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE){buffer.m.planes=planes.data();buffer.length=unsigned(planes.size());}
        if(ioctl(fd.get(),VIDIOC_QUERYBUF,&buffer)||buffer.memory!=V4L2_MEMORY_DMABUF)break;
        const int owner=camera.type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE?planes[0].m.fd:buffer.m.fd;
        if(owner<0)break;
        const auto target="/proc/"+std::to_string(producer.pid)+"/fd/"+std::to_string(owner);struct stat dma{};
        if(linkTarget(target).find("dmabuf")==std::string::npos||stat(target.c_str(),&dma))break;
        const auto metadata=readText("/proc/"+std::to_string(producer.pid)+"/fdinfo/"+std::to_string(owner),8192);
        std::istringstream input(metadata);std::string line;std::uint64_t size=0;
        while(std::getline(input,line))if(line.rfind("size:",0)==0)size=std::stoull(line.substr(5));
        if(!size||size>64u*1024*1024)break;
        camera.buffers.push_back({index,owner,size,dma.st_ino});
    }
    return camera;
}
inline std::vector<Camera> inventory(const Producer& producer){
    Fd pidfd(int(syscall(SYS_pidfd_open,producer.pid,0)));
    if(pidfd.get()<0)throw std::runtime_error("Camera producer handle unavailable");
    const auto current=findProducer();pollfd lifetime{pidfd.get(),POLLIN,0};
    if(current.pid!=producer.pid||current.uid!=producer.uid||current.start!=producer.start||poll(&lifetime,1,0)!=0)
        throw std::runtime_error("Camera producer changed during discovery; manual restart required");
    const MediaGraph graph(producer,pidfd.get());std::map<std::string,unsigned> paths;
    for(unsigned fd:numericEntries("/proc/"+std::to_string(producer.pid)+"/fd",8192)){
        const auto target=linkTarget("/proc/"+std::to_string(producer.pid)+"/fd/"+std::to_string(fd));
        if(target.rfind("/dev/video",0)==0&&target.size()<32&&target.substr(10).find_first_not_of("0123456789")==std::string::npos)paths.emplace(target,fd);
    }
    if(paths.size()>16)throw std::runtime_error("Camera node count exceeds budget");
    std::vector<Camera> cameras;for(const auto& item:paths)cameras.push_back(inspectCamera(item.first,item.second,pidfd.get(),producer,graph));return cameras;
}
inline Json describe(const Camera& camera){
    char fourcc[5]{char(camera.format),char(camera.format>>8),char(camera.format>>16),char(camera.format>>24),0};
    Json buffers=Json::array();for(const auto& buffer:camera.buffers)buffers.push_back({{"index",buffer.index},{"producer_fd",buffer.ownerFd},{"bytes",buffer.length},{"inode",buffer.inode}});
    return {{"path",camera.path},{"sensor",camera.sensor},{"minor",camera.minor},{"format",fourcc},{"width",camera.width},{"height",camera.height},
        {"pitch_reported",camera.pitch},{"producer_video_fd",camera.producerNodeFd},{"planes",camera.planes},{"plane_bytes",camera.planeBytes},{"query_error",camera.error},
        {"queue_mapping",buffers},{"queue_mapping_exact",!camera.buffers.empty()}};
}
} // namespace room_camera_api
