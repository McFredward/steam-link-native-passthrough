#pragma once
#include "native_math.h"
#include "room_font.h"
#include <cstdint>
#include <string>
#include <vector>

namespace room_native {
struct Color {int r,g,b,a=255;};
inline constexpr Color Ink{232,244,244},Muted{157,181,185},Teal{52,224,204},Amber{255,192,88};
class Canvas {
public:
    int width,height;std::vector<std::uint8_t> pixels;
    Canvas(int w,int h):width(w),height(h),pixels(std::size_t(w)*h*4){}
    void clear(){std::fill(pixels.begin(),pixels.end(),0);}
    void put(int x,int y,Color c){
        if(x<0||y<0||x>=width||y>=height)return;
        auto* p=pixels.data()+(std::size_t(y)*width+x)*4;const int a=std::clamp(c.a,0,255),inverse=255-a;
        p[0]=std::uint8_t((std::clamp(c.r,0,255)*a+p[0]*inverse)/255);
        p[1]=std::uint8_t((std::clamp(c.g,0,255)*a+p[1]*inverse)/255);
        p[2]=std::uint8_t((std::clamp(c.b,0,255)*a+p[2]*inverse)/255);
        p[3]=std::uint8_t(a+p[3]*inverse/255);
    }
    void rect(int x,int y,int w,int h,Color c,int radius=0){
        for(int yy=std::max(0,y);yy<std::min(height,y+h);++yy)for(int xx=std::max(0,x);xx<std::min(width,x+w);++xx){
            if(radius){const int dx=std::max({x+radius-xx,0,xx-(x+w-radius-1)}),dy=std::max({y+radius-yy,0,yy-(y+h-radius-1)});
                if(dx*dx+dy*dy>radius*radius)continue;}
            put(xx,yy,c);
        }
    }
    void line(float x,float y,float xx,float yy,Color c,int thickness=1){
        // Clip in parameter space before stepping: offscreen lines cannot
        // create unbounded raster work or integer overflows.
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(xx)||!std::isfinite(yy))return;
        const float dx=xx-x,dy=yy-y;float low=0,high=1;
        const auto clip=[&](float p,float q){if(std::abs(p)<1e-9f)return q>=0;const float t=q/p;
            if(p<0)low=std::max(low,t);else high=std::min(high,t);return low<=high;};
        if(!clip(-dx,x)||!clip(dx,width-1-x)||!clip(-dy,y)||!clip(dy,height-1-y))return;
        xx=x+high*dx;yy=y+high*dy;x+=low*dx;y+=low*dy;
        const int steps=std::max(1,int(std::ceil(std::max(std::abs(xx-x),std::abs(yy-y)))));
        for(int i=0;i<=steps;++i){const float t=float(i)/steps;
            rect(int(std::lround(x+(xx-x)*t))-thickness/2,int(std::lround(y+(yy-y)*t))-thickness/2,thickness,thickness,c);}
    }
    void text(int x,int y,const std::string& value,int size,Color color,int maxWidth=900,int maxLines=3){
        static const auto alpha=[](){std::vector<std::uint8_t> out;out.reserve(1024*448);
            for(const auto& run:room_font::runs)out.insert(out.end(),run.count,run.value);
            return out;}();
        const int start=x,top=y;
        for(std::size_t index=0;index<value.size();){
            if((index==0||value[index-1]==' ')&&value[index]!=' '&&value[index]!='\n'){
                int word=0;for(std::size_t i=index;i<value.size()&&value[i]!=' '&&value[i]!='\n';){
                    unsigned code=static_cast<unsigned char>(value[i++]);
                    if(code>=192&&code<224&&i<value.size())code=((code&31)<<6)|(static_cast<unsigned char>(value[i++])&63);
                    for(const auto& g:room_font::glyphs)if(g.code==code){word+=std::max(1,int(g.advance*size/48));break;}}
                if(x>start&&x+word>start+maxWidth){x=start;y+=size*3/2;}
            }
            unsigned code=static_cast<unsigned char>(value[index++]);
            if(code>=192&&code<224&&index<value.size())code=((code&31)<<6)|(static_cast<unsigned char>(value[index++])&63);
            else if(code>=128)code='?';
            if(code=='\n'){x=start;y+=size*3/2;continue;}
            const room_font::Glyph* glyph=nullptr;
            for(const auto& g:room_font::glyphs)if(g.code==code){glyph=&g;break;}
            if(!glyph)continue;
            const int advance=std::max(1,int(glyph->advance*size/48));
            if(x+advance>start+maxWidth){x=start;y+=size*3/2;}
            if(y-top>=(size*3/2)*maxLines)return;
            const int extent=64*size/48;
            for(int yy=0;yy<extent;++yy)for(int xx=0;xx<extent;++xx){
                const unsigned sx=glyph->x+xx*48/size,sy=glyph->y+yy*48/size;
                Color c=color;c.a=c.a*alpha[sy*1024+sx]/255;if(c.a)put(x+xx,y+yy,c);
            }
            x+=advance;
        }
    }
    void triangle(float ax,float ay,float bx,float by,float cx,float cy,Color color){
        const float area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);if(std::abs(area)<.1f)return;
        const int minX=std::max(0,int(std::floor(std::min({ax,bx,cx})))),maxX=std::min(width-1,int(std::ceil(std::max({ax,bx,cx}))));
        const int minY=std::max(0,int(std::floor(std::min({ay,by,cy})))),maxY=std::min(height-1,int(std::ceil(std::max({ay,by,cy}))));
        for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){
            const float u=((bx-x)*(cy-y)-(by-y)*(cx-x))/area,v=((cx-x)*(ay-y)-(cy-y)*(ax-x))/area;
            if(u>=0&&v>=0&&u+v<=1)put(x,y,color);
        }
    }
};
} // namespace room_native
