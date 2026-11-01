// Bounded public camera inventory. No image acquisition or inferred readiness.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace room_camera_probe {
constexpr unsigned MaxCameras=8;
struct Size { int error=-1;std::uint32_t width=0,height=0,bytes=0; };
struct Intrinsics { int error=-1;float fx=0,fy=0,cx=0,cy=0; };
struct Extrinsics { std::array<float,12> matrix{};bool available=false; };
struct Inventory {
    bool hasCamera=false;int cameraError=-1,countError=-1,layoutError=-1;
    std::int32_t count=0,layout=0;
    std::array<Size,3> sizes{};
    std::array<std::array<Intrinsics,MaxCameras>,3> intrinsics{};
    std::array<Extrinsics,MaxCameras> extrinsics{};
};
struct Api {
    virtual ~Api()=default;
    virtual void camera(bool&,int&)=0;
    virtual void properties(std::int32_t&,int&,std::int32_t&,int&)=0;
    virtual Size size(unsigned)=0;
    virtual Intrinsics intrinsics(unsigned,unsigned)=0;
    virtual void extrinsics(std::array<Extrinsics,MaxCameras>&)=0;
};
inline bool valid(const Size& s){
    return s.error==0&&s.width&&s.height&&s.width<=8192&&s.height<=8192
        &&std::uint64_t(s.width)*s.height*4==s.bytes&&s.bytes<=64u*1024*1024;
}
inline bool valid(const Intrinsics& i){
    return i.error==0&&std::isfinite(i.fx)&&std::isfinite(i.fy)&&std::isfinite(i.cx)&&std::isfinite(i.cy)
        &&i.fx>0&&i.fy>0&&i.fx<=100000&&i.fy<=100000&&i.cx>=0&&i.cy>=0&&i.cx<=8192&&i.cy<=8192;
}
inline bool valid(const Extrinsics& e){
    if(!e.available)return false;
    const auto& m=e.matrix;
    for(float f:m)if(!std::isfinite(f)||std::abs(f)>1000)return false;
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){
        float product=0;for(unsigned c=0;c<3;++c)product+=m[a*4+c]*m[b*4+c];
        if(std::abs(product-(a==b?1.f:0.f))>.02f)return false;
    }
    const float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
    return std::abs(det-1.f)<.02f;
}
inline Inventory inspect(Api& api){
    Inventory out;api.camera(out.hasCamera,out.cameraError);
    api.properties(out.count,out.countError,out.layout,out.layoutError);
    if(out.cameraError!=0||!out.hasCamera)return out;
    const unsigned count=out.countError==0&&out.count>0&&out.count<=int(MaxCameras)?unsigned(out.count):0;
    for(unsigned kind=0;kind<3;++kind){
        out.sizes[kind]=api.size(kind);
        if(valid(out.sizes[kind]))for(unsigned index=0;index<count;++index)out.intrinsics[kind][index]=api.intrinsics(kind,index);
    }
    if(count)api.extrinsics(out.extrinsics);
    return out;
}
inline std::string report(const Inventory& data){
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
    const bool camera=data.cameraError==0&&data.hasCamera;
    const unsigned count=data.countError==0&&data.count>0&&data.count<=int(MaxCameras)?unsigned(data.count):0;
    out<<"{\"schema\":\"sfnp-room-camera-inventory-v1\",\"public_api\":\"IVRTrackedCamera_006\","
       <<"\"frame_identity_verified\":true,\"arcturus_identity_verified\":false,\"camera_images_read\":false,"
       <<"\"stream_acquired\":false,\"room_scan_ready\":false,\"camera_error\":"<<data.cameraError
       <<",\"public_camera_reported\":"<<(camera?"true":"false")<<",\"count_error\":"<<data.countError
       <<",\"reported_camera_count\":"<<data.count<<",\"bounded_camera_count\":"<<count
       <<",\"layout_error\":"<<data.layoutError<<",\"reported_layout\":"<<data.layout
       <<",\"formats\":[";
    for(unsigned kind=0;kind<3;++kind){
        if(kind)out<<',';
        const auto& size=data.sizes[kind];const bool extent=valid(size);
        out<<"{\"type\":"<<kind<<",\"error\":"<<size.error<<",\"extent_valid\":"<<(extent?"true":"false")
           <<",\"width\":"<<size.width<<",\"height\":"<<size.height<<",\"rgba_bytes\":"<<size.bytes<<",\"intrinsics\":[";
        for(unsigned index=0;index<count;++index){
            if(index)out<<',';
            const auto& i=data.intrinsics[kind][index];const bool okay=extent&&valid(i);
            out<<"{\"index\":"<<index<<",\"error\":"<<i.error<<",\"valid\":"<<(okay?"true":"false")<<",\"fx_fy_cx_cy\":";
            if(okay)out<<'['<<i.fx<<','<<i.fy<<','<<i.cx<<','<<i.cy<<']';else out<<"null";
            out<<'}';
        }
        out<<"]}";
    }
    out<<"],\"camera_to_head\":[";
    for(unsigned index=0;index<count;++index){
        if(index)out<<',';
        const auto& e=data.extrinsics[index];out<<"{\"index\":"<<index<<",\"valid\":"<<(valid(e)?"true":"false")<<",\"matrix\":";
        if(valid(e)){out<<'[';for(unsigned n=0;n<12;++n){if(n)out<<',';out<<e.matrix[n];}out<<']';}else out<<"null";
        out<<'}';
    }
    out<<"],\"exposure_pose_association_verified\":false,\"pixel_layout_verified\":false}\n";
    return out.str();
}
} // namespace room_camera_probe
