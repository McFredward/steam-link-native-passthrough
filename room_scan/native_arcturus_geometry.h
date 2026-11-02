// Factory KB fisheye rectification for the active 2464-square NV12 image.
// EEPROM cropRegion describes the ISP input crop, already present in these
// active images; subtracting it again produces a different, invalid geometry.
#pragma once
#include "native_camera.h"
#include "native_arcturus_timing.h"
#include "native_triangulation.h"

namespace room_native {
class ArcturusRectification {
    struct Lens {float fx=0,fy=0,cx=0,cy=0;std::array<float,4> k{};Mat optical;};
    struct Pixel {float u=-1,v=-1;};
    std::array<Lens,2> lenses_{};std::array<std::vector<Pixel>,2> maps_;
public:
    struct RectifiedImage {std::vector<std::uint8_t> pixels,support;std::vector<V3> origins;};
    Mat headFromRectified;float baseline=0;Json descriptor;
    std::array<std::vector<std::uint8_t>,2> support;
    Json exportRig()const{
        const auto matrix=[](const Mat& m){Json out=Json::array();for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)out.push_back(m.m[r][c]);return out;};
        Json eyes=Json::array();for(const auto& lens:lenses_)eyes.push_back({{"intrinsics",{lens.fx,lens.fy,lens.cx,lens.cy}},
            {"k",lens.k},{"head_from_optical",matrix(lens.optical)}});
        return {{"eyes",eyes},{"head_from_rectified",matrix(headFromRectified)},{"baseline_m",baseline}};
    }
    static std::array<float,2> project(const std::array<float,4>& in,const std::array<float,4>& k,V3 ray){
        if(!std::isfinite(ray.x)||!std::isfinite(ray.y)||!std::isfinite(ray.z)||ray.z<=0)return {-1,-1};
        const float r=std::hypot(ray.x,ray.y),theta=std::atan2(r,ray.z),t2=theta*theta;
        const float distorted=theta*(1+t2*(k[0]+t2*(k[1]+t2*(k[2]+t2*k[3]))));
        const float scale=r>1e-8f?distorted/r:1.f/ray.z;
        return {in[0]*ray.x*scale+in[2],in[1]*ray.y*scale+in[3]};
    }
    void configure(const Json& rig,const Json& device){
        const auto& cameras=rig.at("cameras");
        if(!cameras.is_array()||cameras.size()>8)throw std::runtime_error("Unsupported Arcturus calibration inventory");
        const Mat headFromCad=inverse(factoryPose(device.at("head"),1));std::array<bool,2> found{};
        descriptor={{"source_extent",{2464,2464}},{"sample_step",4},{"source_pitch",3328},{"rectified_extent",{320,240}},
            {"rectified_intrinsics",{220.,220.,159.5,119.5}},{"cameras",Json::array()},{"head",device.at("head")}};
        for(const auto& camera:cameras){
            const auto role=camera.at("sourceCamera").get<std::string>();const int index=role=="passthrough_left"?0:role=="passthrough_right"?1:-1;
            if(index<0)continue;
            if(found[unsigned(index)]||camera.at("width")!=2464||camera.at("height")!=2464||camera.at("intrinsics").size()!=1)
                throw std::runtime_error("Arcturus calibration extent or identity invalid");
            found[unsigned(index)]=true;auto& lens=lenses_[unsigned(index)];const auto& in=camera.at("intrinsics").at(0);
            if(in.at("cameraModel")!="kb")throw std::runtime_error("Unsupported Arcturus optical model");
            lens.fx=in.at("fx");lens.fy=in.at("fy");lens.cx=in.at("cx");lens.cy=in.at("cy");
            for(unsigned i=0;i<4;++i)lens.k[i]=in.at("k"+std::to_string(i+1)).get<float>();
            for(float v:{lens.fx,lens.fy,lens.cx,lens.cy,lens.k[0],lens.k[1],lens.k[2],lens.k[3]})
                if(!std::isfinite(v))throw std::runtime_error("Nonfinite Arcturus lens");
            if(lens.fx<500||lens.fx>4000||lens.fy<500||lens.fy>4000||lens.cx<0||lens.cx>=2464||lens.cy<0||lens.cy>=2464)
                throw std::runtime_error("Unsupported Arcturus lens dimensions");
            lens.optical=multiply(headFromCad,factoryPose(camera.at("extrinsics"),.001f));
            descriptor["cameras"].push_back(camera);
        }
        if(!found[0]||!found[1])throw std::runtime_error("Arcturus stereo calibration missing");
        const V3 left=position(lenses_[0].optical),right=position(lenses_[1].optical);
        baseline=length(right-left);if(baseline<.03f||baseline>.10f)throw std::runtime_error("Arcturus metric baseline invalid");
        const V3 x=normalized(right-left),forward=normalized(column(lenses_[0].optical,2)+column(lenses_[1].optical,2)),down=normalized(cross(forward,x)),z=normalized(cross(x,down));
        const std::array<V3,4> axes{x,down*(-1),z*(-1),left};headFromRectified=Mat{};
        for(unsigned c=0;c<4;++c){headFromRectified.m[0][c]=axes[c].x;headFromRectified.m[1][c]=axes[c].y;headFromRectified.m[2][c]=axes[c].z;}
        if(!rigid(headFromRectified))throw std::runtime_error("Invalid Arcturus rectification axes");
        for(unsigned eye=0;eye<2;++eye){
            auto& map=maps_[eye];map.assign(320*240,Pixel{});const auto& lens=lenses_[eye];
            for(unsigned v=0;v<240;++v)for(unsigned u=0;u<320;++u){
                const V3 ray=x*((float(u)-159.5f)/220)+down*((float(v)-119.5f)/220)+z;
                const V3 c={dot(column(lens.optical,0),ray),dot(column(lens.optical,1),ray),dot(column(lens.optical,2),ray)};
                const auto uv=project({lens.fx,lens.fy,lens.cx,lens.cy},lens.k,c);
                const float a=uv[0]/4,b=uv[1]/4;
                if(a>=0&&b>=0&&a<615&&b<615)map[v*320+u]={a,b};
            }
            support[eye].assign(320*240,0);
            for(int v=3;v<237;++v)for(int u=3;u<317;++u){bool valid=true;
                for(int dy=-3;dy<=3&&valid;++dy)for(int dx=-3;dx<=3;++dx)if(map[std::size_t(v+dy)*320+u+dx].u<0){valid=false;break;}
                support[eye][std::size_t(v)*320+u]=valid;
            }
        }
    }
    std::vector<std::uint8_t> rectify(unsigned eye,const std::vector<std::uint8_t>& input)const{
        if(eye>1||input.size()!=616*616||maps_[eye].size()!=320*240)throw std::runtime_error("Arcturus source image extent invalid");
        std::vector<std::uint8_t> out(320*240,0);
        for(std::size_t i=0;i<out.size();++i){const auto uv=maps_[eye][i];if(uv.u<0)continue;
            const unsigned u=unsigned(uv.u),v=unsigned(uv.v);const float a=uv.u-u,b=uv.v-v;
            const auto p=std::size_t(v)*616+u;
            out[i]=std::uint8_t(std::clamp((1-b)*((1-a)*input[p]+a*input[p+1])+b*((1-a)*input[p+616]+a*input[p+617]),0.f,255.f));
        }
        return out;
    }
    template<class Poses>RectifiedImage rectifyTimed(unsigned eye,const std::vector<std::uint8_t>& input,
            const ArcturusTiming& timing,const Mat& reference,const Poses& poses)const{
        if(eye>1||input.size()!=616*616||maps_[eye].size()!=320*240)throw std::runtime_error("Arcturus source image extent invalid");
        RectifiedImage out;out.pixels.assign(320*240,0);out.support.assign(320*240,0);out.origins.resize(320*240);std::vector<std::uint8_t> valid(320*240,0);
        const auto& lens=lenses_[eye];
        std::array<Mat,2464> rowPoses;for(unsigned row=0;row<rowPoses.size();++row)rowPoses[row]=poses.at(timing.atRow(float(row)));
        for(unsigned v=0;v<240;++v)for(unsigned u=0;u<320;++u){
            const auto index=v*320+u;auto uv=maps_[eye][index];if(uv.u<0)continue;
            const V3 ray=column(headFromRectified,0)*((float(u)-159.5f)/220)
                +column(headFromRectified,1)*(-(float(v)-119.5f)/220)+column(headFromRectified,2)*(-1);
            const V3 world=transform(reference,ray)-position(reference);
            for(unsigned iteration=0;iteration<2;++iteration){
                const Mat& row=rowPoses[unsigned(std::clamp(uv.v*4+.5f,0.f,2463.f))];
                const V3 head={dot(column(row,0),world),dot(column(row,1),world),dot(column(row,2),world)};
                const V3 optical={dot(column(lens.optical,0),head),dot(column(lens.optical,1),head),dot(column(lens.optical,2),head)};
                const auto next=project({lens.fx,lens.fy,lens.cx,lens.cy},lens.k,optical);uv={next[0]/4,next[1]/4};
                if(uv.u<0||uv.v<0||uv.u>=615||uv.v>=615)break;
            }
            if(uv.u<0||uv.v<0||uv.u>=615||uv.v>=615)continue;
            const unsigned x=unsigned(uv.u),y=unsigned(uv.v);const float a=uv.u-x,b=uv.v-y;const auto p=std::size_t(y)*616+x;
            out.pixels[index]=std::uint8_t(std::clamp((1-b)*((1-a)*input[p]+a*input[p+1])+b*((1-a)*input[p+616]+a*input[p+617]),0.f,255.f));valid[index]=1;
            out.origins[index]=transform(rowPoses[unsigned(std::clamp(uv.v*4+.5f,0.f,2463.f))],position(lens.optical));
        }
        for(int v=3;v<237;++v)for(int u=3;u<317;++u){bool supported=true;
            for(int dy=-3;dy<=3&&supported;++dy)for(int dx=-3;dx<=3;++dx)if(!valid[std::size_t(v+dy)*320+u+dx]){supported=false;break;}
            out.support[std::size_t(v)*320+u]=supported;
        }
        return out;
    }
};
inline StereoResult exposureAlignedDepth(const StereoResult& matched,
        const std::array<ArcturusRectification::RectifiedImage,2>& images,const Mat& roomFromRectified,float baseline){
    for(const auto& image:images)if(image.origins.size()!=320*240||image.support.size()!=320*240)
        throw std::runtime_error("Exposure camera origins are unavailable");
    StereoResult out;const Mat rectifiedFromRoom=inverse(roomFromRectified);
    const auto ray=[&](float u,float v){const V3 p{(u-159.5f)/220,-(v-119.5f)/220,-1};
        return transform(roomFromRectified,p)-position(roomFromRectified);};
    for(unsigned i=0;i<matched.depth.size();++i){const float z=matched.depth[i];if(z<=0)continue;
        const unsigned u=(i%160)*2,v=(i/160)*2;const auto leftIndex=std::size_t(v)*320+u;
        const float rightU=float(u)-220*baseline/z;if(rightU<0||rightU>=319)continue;
        const unsigned lo=unsigned(rightU);const auto rightIndex=std::size_t(v)*320+lo;const float t=rightU-lo;
        if(!images[0].support[leftIndex]||!images[1].support[rightIndex]||!images[1].support[rightIndex+1])continue;
        const V3 rightOrigin=images[1].origins[rightIndex]*(1-t)+images[1].origins[rightIndex+1]*t;
        const auto hit=intersectStereoRays(images[0].origins[leftIndex],ray(float(u),float(v)),rightOrigin,ray(rightU,float(v)));
        if(!hit)continue;
        const V3 local=transform(rectifiedFromRoom,hit->point);const float depth=-local.z;if(depth<.25f||depth>6)continue;
        const int x=int(std::lround((220*local.x/depth+159.5f)/2)),y=int(std::lround((-220*local.y/depth+119.5f)/2));
        if(x<0||y<0||x>=160||y>=120)continue;
        const auto target=std::size_t(y)*160+x;
        if(out.depth[target]>0&&out.depth[target]<=depth)continue;
        out.depth[target]=depth;out.confidence[target]=hit->separation>.002f?std::min(.6f,matched.confidence[i]):matched.confidence[i];
    }
    return out;
}
} // namespace room_native
