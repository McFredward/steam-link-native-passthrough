// Manual prototype source. Exact kernel queue mapping, no heuristic FD order.
// Diagnostic image access alone never attests eye identity or an exposure pose.
#pragma once
#include "trace_reader.h"
#include "luma_layout.h"
#include <grp.h>
#include <linux/capability.h>
#include <linux/dma-buf.h>
#include <sys/prctl.h>
#include <optional>

namespace room_camera_api {
struct TargetUser {unsigned uid=0,gid=0;};
inline TargetUser targetUser(const Producer& producer){
    TargetUser user{unsigned(getuid()),unsigned(getgid())};
    if(geteuid()==0){
        const char* uid=std::getenv("SUDO_UID"),*gid=std::getenv("SUDO_GID");
        if(!uid||!gid||std::string(uid).find_first_not_of("0123456789")!=std::string::npos||std::string(gid).find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("Camera prototype requires the original sudo user");
        user={unsigned(std::stoul(uid)),unsigned(std::stoul(gid))};
    }
    if(!user.uid||user.uid!=producer.uid)throw std::runtime_error("Camera producer must belong to the invoking user");
    return user;
}
inline void dropPrivileges(TargetUser target){
    if(geteuid()==0){
        if(prctl(PR_SET_KEEPCAPS,0,0,0,0)||setgroups(0,nullptr)||setresgid(target.gid,target.gid,target.gid)||setresuid(target.uid,target.uid,target.uid))
            throw std::runtime_error("Camera setup privileges could not be dropped");
    }
    __user_cap_header_struct header{_LINUX_CAPABILITY_VERSION_3,0};__user_cap_data_struct capabilities[2]{};
    if(syscall(SYS_capget,&header,capabilities)||getuid()!=target.uid||geteuid()!=target.uid||getgid()!=target.gid||getegid()!=target.gid
        ||capabilities[0].effective||capabilities[1].effective||capabilities[0].permitted||capabilities[1].permitted
        ||capabilities[0].inheritable||capabilities[1].inheritable)
        throw std::runtime_error("Camera reader still has unexpected privileges");
}
class BorrowedFrames {
    struct Mapping {
        Fd descriptor;void* data=MAP_FAILED;std::size_t bytes=0;unsigned index=0;int owner=-1;std::uint64_t inode=0;
        Mapping(Fd fd,void* pointer,std::size_t size,unsigned slot,int source,std::uint64_t ino):
            descriptor(std::move(fd)),data(pointer),bytes(size),index(slot),owner(source),inode(ino){}
        Mapping(const Mapping&)=delete;Mapping& operator=(const Mapping&)=delete;
        Mapping(Mapping&& other)noexcept:descriptor(std::move(other.descriptor)),data(std::exchange(other.data,MAP_FAILED)),
            bytes(other.bytes),index(other.index),owner(other.owner),inode(other.inode){}
        ~Mapping(){if(data!=MAP_FAILED)munmap(data,bytes);}
    };
    struct Stream {Camera camera;Fd node;std::vector<Mapping> buffers;unsigned sequence=0;std::uint64_t lastPublish=0;};
    Producer producer_;Fd pidfd_;std::vector<Stream> streams_;
    std::map<std::string,std::uint64_t> counters_;
    static int queuedFd(const Stream& stream,unsigned index){
        v4l2_buffer buffer{};std::array<v4l2_plane,VIDEO_MAX_PLANES> planes{};
        buffer.index=index;buffer.type=stream.camera.type;buffer.memory=V4L2_MEMORY_DMABUF;
        buffer.m.planes=planes.data();buffer.length=unsigned(planes.size());
        if(ioctl(stream.node.get(),VIDIOC_QUERYBUF,&buffer)||buffer.memory!=V4L2_MEMORY_DMABUF)return -1;
        return planes[0].m.fd;
    }
public:
    explicit BorrowedFrames(Producer producer):producer_(std::move(producer)),pidfd_(int(syscall(SYS_pidfd_open,producer_.pid,0))){
        if(pidfd_.get()<0)throw std::runtime_error("Camera producer descriptor unavailable");
        for(auto& camera:inventory(producer_)){
            if(camera.sensor.rfind("arcimx616 ",0)!=0)continue;
            if(camera.error||camera.format!=V4L2_PIX_FMT_NV12||camera.width!=2464||camera.height!=2464||camera.type!=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
                ||camera.buffers.empty()||camera.buffers.size()>32)throw std::runtime_error("Arcturus buffer layout is not established");
            Stream stream;stream.node=Fd(int(syscall(SYS_pidfd_getfd,pidfd_.get(),camera.producerNodeFd,0)));stream.camera=std::move(camera);
            if(stream.node.get()<0)throw std::runtime_error("Arcturus queue reader unavailable");
            for(const auto& buffer:stream.camera.buffers){
                Fd fd(int(syscall(SYS_pidfd_getfd,pidfd_.get(),buffer.ownerFd,0)));struct stat info{};
                if(fd.get()<0)throw std::runtime_error("Arcturus buffer borrowing needs temporary ptrace permission; errno="+std::to_string(errno));
                if(fstat(fd.get(),&info)||info.st_ino!=buffer.inode||queuedFd(stream,buffer.index)!=buffer.ownerFd)
                    throw std::runtime_error("Arcturus buffer binding changed");
                void* data=mmap(nullptr,std::size_t(buffer.length),PROT_READ,MAP_SHARED,fd.get(),0);
                if(data==MAP_FAILED)throw std::runtime_error("Arcturus read-only buffer mapping failed");
                stream.buffers.push_back({std::move(fd),data,std::size_t(buffer.length),buffer.index,buffer.ownerFd,buffer.inode});
            }
            streams_.push_back(std::move(stream));
        }
        if(streams_.size()!=2)throw std::runtime_error("Exactly two Arcturus streams are required");
    }
    bool alive()const{
        pollfd event{pidfd_.get(),POLLIN,0};if(poll(&event,1,0)!=0)return false;
        for(const auto& stream:streams_)if(linkTarget("/proc/"+std::to_string(producer_.pid)+"/fd/"+std::to_string(stream.camera.producerNodeFd))!=stream.camera.path)return false;
        return true;
    }
    Json describe()const{
        Json streams=Json::array();for(const auto& stream:streams_)streams.push_back(room_camera_api::describe(stream.camera));
        return {{"schema","sfnp-native-camera-api-v1"},{"producer_pid",producer_.pid},{"streams",streams},{"camera_settings_changed",false},
            {"producer_start_ticks",producer_.start},{"boot_changes",false},{"maps_read_only",true},{"eye_identity_verified",false},
            {"pixel_layout_verified",false},{"exposure_pose_verified",false},{"room_scan_ready",false},{"counters",counters_}};
    }
    struct Image {Json metadata;std::vector<std::uint8_t> pixels;};
    std::optional<Image> copy(const Completion& event){
        for(auto& stream:streams_){
            if(stream.camera.minor!=event.minor)continue;
            ++counters_["color_events"];
            const auto age=monotonicNs()-event.dequeue;
            if(age>20000000u){++counters_["old_completion"];return std::nullopt;}
            if(!event.capture||!event.dequeue){++counters_["missing_time"];return std::nullopt;}
            if(event.sequence<=stream.sequence){++counters_["repeated_sequence"];return std::nullopt;}
            if(event.flags&V4L2_BUF_FLAG_ERROR){++counters_["buffer_error_flag"];return std::nullopt;}
            stream.sequence=event.sequence;
            // A latest-image API needs no backlog and no repeated work per client.
            if(event.dequeue-stream.lastPublish<100000000u){++counters_["rate_skips"];return std::nullopt;}
            const auto mapping=std::find_if(stream.buffers.begin(),stream.buffers.end(),[&](const auto& slot){return slot.index==event.index;});
            if(mapping==stream.buffers.end()||queuedFd(stream,event.index)!=mapping->owner)throw std::runtime_error("Arcturus queue mapping changed; manual restart required");
            const auto producerBuffer="/proc/"+std::to_string(producer_.pid)+"/fd/"+std::to_string(mapping->owner);struct stat current{};
            if(stat(producerBuffer.c_str(),&current)||current.st_ino!=mapping->inode)throw std::runtime_error("Arcturus producer replaced its buffer");
            constexpr unsigned height=616,pitch=3328;
            if(mapping->bytes<std::size_t(pitch)*2464)throw std::runtime_error("Arcturus luma extent is short");
            std::vector<std::uint8_t> first,second;first.reserve(616*616);second.reserve(616*616);const auto begin=monotonicNs();LumaLayout layout;
            const auto read=[&](std::vector<std::uint8_t>& out,bool examine){
                dma_buf_sync sync{DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ};
                if(ioctl(mapping->descriptor.get(),DMA_BUF_IOCTL_SYNC,&sync))throw std::runtime_error("Arcturus read cache sync failed");
                const auto* data=static_cast<const std::uint8_t*>(mapping->data);
                if(examine)layout=examineLuma(data,mapping->bytes);
                copyLuma(data,mapping->bytes,layout,out);
                sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ;
                if(ioctl(mapping->descriptor.get(),DMA_BUF_IOCTL_SYNC,&sync))throw std::runtime_error("Arcturus read cache sync end failed");
            };
            read(first,true);read(second,false);const auto end=monotonicNs();
            if(first!=second||queuedFd(stream,event.index)!=mapping->owner){++counters_["unstable_copy"];return std::nullopt;}
            stream.lastPublish=event.dequeue;
            ++counters_["published"];
            timespec raw{};const auto clockBefore=monotonicNs();clock_gettime(CLOCK_MONOTONIC_RAW,&raw);const auto clockAfter=monotonicNs();
            Json metadata={{"schema","sfnp-native-camera-frame-v1"},{"sensor",stream.camera.sensor},{"node",stream.camera.path},{"width",layout.width()},{"height",height},
                {"producer_pid",producer_.pid},{"producer_start_ticks",producer_.start},{"source_pitch_bytes",pitch},{"source_height",2464},
                {"encoding",layout.packed10?"arcturus-mipi10-luma-candidate":"nv12-u8-luma-candidate"},{"packing_adjacent_differences",layout.adjacent},{"packing_periodic_ratio",layout.periodicRatio},
                {"sequence",event.sequence},{"buffer_index",event.index},{"buffer_inode",mapping->inode},
                {"capture_ticks",event.capture},{"dequeue_monotonic_ns",event.dequeue},{"copy_begin_ns",begin},{"copy_end_ns",end},{"v4l2_flags",event.flags},
                {"clock_sample",{{"monotonic_before_ns",clockBefore},{"raw_ns",std::uint64_t(raw.tv_sec)*1000000000u+std::uint64_t(raw.tv_nsec)},{"monotonic_after_ns",clockAfter}}},
                {"queue_mapping_exact",true},{"copy_stable",true},{"eye_identity_verified",false},{"pixel_layout_verified",false},{"exposure_pose_verified",false},{"room_scan_ready",false}};
            return Image{std::move(metadata),std::move(first)};
        }
        return std::nullopt;
    }
};
} // namespace room_camera_api
