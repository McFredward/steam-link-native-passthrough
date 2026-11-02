// Depth-tested measured surfaces. Unobserved space stays transparent.
#pragma once
#include "native_raster.h"
#include <limits>

namespace room_native {
struct SurfacePoint {float x=0,y=0,z=0;V3 room;bool valid=false;};
class SurfaceCanvas {
    Canvas& image_;std::vector<float>& depth_;
public:
    SurfaceCanvas(Canvas& image,std::vector<float>& depth):image_(image),depth_(depth){
        depth_.resize(std::size_t(image.width)*image.height);
        std::fill(depth_.begin(),depth_.end(),std::numeric_limits<float>::infinity());
    }
    void face(const SurfacePoint& a,const SurfacePoint& b,const SurfacePoint& c,Color color,bool uncertain,float bias=0){
        const float area=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);if(std::abs(area)<.1f)return;
        const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))),x1=std::min(image_.width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        const int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))),y1=std::min(image_.height-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            const float u=((b.x-x)*(c.y-y)-(b.y-y)*(c.x-x))/area,v=((c.x-x)*(a.y-y)-(c.y-y)*(a.x-x))/area,w=1-u-v;
            if(u<0||v<0||w<0)continue;
            const float reciprocal=u/a.z+v/b.z+w/c.z;if(reciprocal<=0)continue;
            const float z=1/reciprocal;const auto index=std::size_t(y)*image_.width+x;if(z-bias>=depth_[index])continue;
            depth_[index]=z-bias;Color fill=color;
            if(uncertain){
                // A room-space hatch does not swim across a surface as the
                // viewer changes direction or the display extent changes.
                const V3 p=(a.room*(u/a.z)+b.room*(v/b.z)+c.room*(w/c.z))*z;
                if((int(std::floor((p.x+p.y+p.z)*35))&3)==0)fill.a=std::min(130,fill.a+30);
            }
            auto* pixel=image_.pixels.data()+index*4;std::fill(pixel,pixel+4,0);image_.put(x,y,fill);
        }
    }
    void line(const SurfacePoint& a,const SurfacePoint& b,Color color){
        const float dx=b.x-a.x,dy=b.y-a.y;float low=0,high=1;
        const auto clip=[&](float p,float q){if(std::abs(p)<1e-9f)return q>=0;const float t=q/p;
            if(p<0)low=std::max(low,t);else high=std::min(high,t);return low<=high;};
        if(!clip(-dx,a.x)||!clip(dx,image_.width-1-a.x)||!clip(-dy,a.y)||!clip(dy,image_.height-1-a.y))return;
        const int count=std::max(1,int(std::ceil(std::max(std::abs(dx),std::abs(dy))*(high-low))));
        for(int n=0;n<=count;++n){const float t=low+(high-low)*float(n)/count;
            const int x=int(std::lround(a.x+dx*t)),y=int(std::lround(a.y+dy*t));
            if(x<0||y<0||x>=image_.width||y>=image_.height)continue;
            const float z=1/((1-t)/a.z+t/b.z);const auto index=std::size_t(y)*image_.width+x;
            if(z<=depth_[index]+.002f*(1+z))image_.put(x,y,color);
        }
    }
};
} // namespace room_native
