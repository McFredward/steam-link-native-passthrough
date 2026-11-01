#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace room_native {
struct V3 { float x=0,y=0,z=0; };
inline V3 operator+(V3 a,V3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline V3 operator-(V3 a,V3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline V3 operator*(V3 a,float s){return {a.x*s,a.y*s,a.z*s};}
inline float dot(V3 a,V3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline V3 cross(V3 a,V3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(V3 a){return std::sqrt(dot(a,a));}
inline V3 normalized(V3 a){const float n=length(a);if(!std::isfinite(n)||n<1e-6f)throw std::runtime_error("Invalid direction");return a*(1/n);}
struct Mat {float m[3][4]{{1,0,0,0},{0,1,0,0},{0,0,1,0}};};
template<class T>inline Mat fromNative(const T& value){Mat out;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)out.m[r][c]=value.m[r][c];return out;}
inline V3 transform(const Mat& a,V3 v){return {a.m[0][0]*v.x+a.m[0][1]*v.y+a.m[0][2]*v.z+a.m[0][3],
    a.m[1][0]*v.x+a.m[1][1]*v.y+a.m[1][2]*v.z+a.m[1][3],a.m[2][0]*v.x+a.m[2][1]*v.y+a.m[2][2]*v.z+a.m[2][3]};}
inline V3 position(const Mat& a){return {a.m[0][3],a.m[1][3],a.m[2][3]};}
inline V3 column(const Mat& a,unsigned c){return {a.m[0][c],a.m[1][c],a.m[2][c]};}
inline Mat inverse(const Mat& a){Mat b;for(unsigned r=0;r<3;++r){for(unsigned c=0;c<3;++c)b.m[r][c]=a.m[c][r];
    b.m[r][3]=-(b.m[r][0]*a.m[0][3]+b.m[r][1]*a.m[1][3]+b.m[r][2]*a.m[2][3]);}return b;}
inline Mat multiply(const Mat& a,const Mat& b){Mat c;for(unsigned r=0;r<3;++r){for(unsigned k=0;k<3;++k){c.m[r][k]=0;for(unsigned j=0;j<3;++j)c.m[r][k]+=a.m[r][j]*b.m[j][k];}
    c.m[r][3]=a.m[r][3];for(unsigned j=0;j<3;++j)c.m[r][3]+=a.m[r][j]*b.m[j][3];}return c;}
inline bool rigid(const Mat& a){for(const auto& r:a.m)for(float n:r)if(!std::isfinite(n)||std::abs(n)>1000)return false;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)if(std::abs(dot(column(a,r),column(a,c))-(r==c?1.f:0.f))>.01f)return false;
    return std::abs(dot(column(a,0),cross(column(a,1),column(a,2)))-1)<.01f;}
inline std::array<float,4> quaternion(const Mat& m){
    std::array<float,4> q{};const float trace=m.m[0][0]+m.m[1][1]+m.m[2][2];
    if(trace>0){const float s=std::sqrt(trace+1)*2;q={(m.m[2][1]-m.m[1][2])/s,(m.m[0][2]-m.m[2][0])/s,(m.m[1][0]-m.m[0][1])/s,s/4};}
    else{unsigned i=0;for(unsigned j=1;j<3;++j)if(m.m[j][j]>m.m[i][i])i=j;const unsigned j=(i+1)%3,k=(i+2)%3;
        const float s=std::sqrt(1+m.m[i][i]-m.m[j][j]-m.m[k][k])*2;q[i]=s/4;q[j]=(m.m[i][j]+m.m[j][i])/s;q[k]=(m.m[i][k]+m.m[k][i])/s;q[3]=(m.m[k][j]-m.m[j][k])/s;}
    float n=0;for(float x:q)n+=x*x;n=std::sqrt(n);for(float& x:q)x/=n;return q;
}
inline Mat pose(V3 p,const std::array<float,4>& q){const auto [x,y,z,w]=q;Mat m;
    m.m[0][0]=1-2*(y*y+z*z);m.m[0][1]=2*(x*y-z*w);m.m[0][2]=2*(x*z+y*w);
    m.m[1][0]=2*(x*y+z*w);m.m[1][1]=1-2*(x*x+z*z);m.m[1][2]=2*(y*z-x*w);
    m.m[2][0]=2*(x*z-y*w);m.m[2][1]=2*(y*z+x*w);m.m[2][2]=1-2*(x*x+y*y);
    m.m[0][3]=p.x;m.m[1][3]=p.y;m.m[2][3]=p.z;return m;}
struct Frustum {float left=-1,right=1,top=-1,bottom=1;};
inline bool project(V3 p,const Frustum& f,int width,int height,float& x,float& y){
    if(-p.z<.08f||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;
    x=(p.x/-p.z-f.left)/(f.right-f.left)*width;y=(-p.y/-p.z-f.top)/(f.bottom-f.top)*height;
    return std::isfinite(x)&&std::isfinite(y)&&std::abs(x)<width*16&&std::abs(y)<height*16;
}
} // namespace room_native
