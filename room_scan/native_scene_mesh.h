// World-space vertices for native MR rendering. Unknown space has no geometry.
#pragma once
#include "native_view.h"

namespace room_native {
struct SceneVertex {
    float position[3],color[4],barycentric[3],firstSeen,style;
};
static_assert(sizeof(SceneVertex)==48);
inline constexpr std::size_t SceneVertexLimit=1200000;
class SceneMeshBuilder {
    void triangle(V3 a,V3 b,V3 c,std::array<float,4> color,float first,float style){
        if(vertices.size()+3>SceneVertexLimit)throw std::runtime_error("Room scene vertex budget reached");
        unsigned index=0;for(V3 p:{a,b,c}){
            for(float v:{p.x,p.y,p.z})if(!std::isfinite(v)||std::abs(v)>1000)throw std::runtime_error("Invalid room scene vertex");
            SceneVertex v{{p.x,p.y,p.z},{color[0],color[1],color[2],color[3]},{0,0,0},first,style};
            v.barycentric[index++]=1;vertices.push_back(v);}
    }
public:
    std::vector<SceneVertex> vertices;
    void measuredSurface(V3 a,V3 b,V3 c,bool reliable,float first){
        triangle(a,b,c,{.12f,.84f,.78f,1},first,reliable?7.f:6.f);
    }
    void line(V3 a,V3 b,std::array<float,4> color,float radius=.0015f){
        const auto delta=b-a;if(length(delta)<1e-6f)return;
        const auto direction=normalized(delta);const auto guide=std::abs(direction.y)<.9f?V3{0,1,0}:V3{1,0,0};
        const auto x=normalized(cross(direction,guide))*radius,y=normalized(cross(direction,x))*radius;
        const std::array<V3,4> offsets{x+y,x-y,(x+y)*-1,y-x};
        for(unsigned i=0;i<4;++i){const auto j=(i+1)%4;
            triangle(a+offsets[i],b+offsets[i],b+offsets[j],color,0,4);
            triangle(a+offsets[i],b+offsets[j],a+offsets[j],color,0,4);}
    }
    void marker(V3 point){
        for(auto axis:{V3{.012f,0,0},V3{0,.012f,0},V3{0,0,.012f}})line(point-axis,point+axis,{.9f,1,1,.95f},.0012f);
    }
    void update(const MeshView& view){
        vertices.clear();if(!view.allowed)return;
        std::size_t face=0;
        for(const auto& t:view.triangles){
            const bool uncertain=view.quality[t[0]]==1||view.quality[t[1]]==1||view.quality[t[2]]==1;
            const bool reliable=view.quality[t[0]]==2&&view.quality[t[1]]==2&&view.quality[t[2]]==2;
            const float first=float(face<view.faceFirstSeen.size()?view.faceFirstSeen[face]:std::max({view.firstSeen[t[0]],view.firstSeen[t[1]],view.firstSeen[t[2]]}));++face;
            triangle(view.vertices[t[0]],view.vertices[t[1]],view.vertices[t[2]],{.7f,.8f,.83f,1},first,uncertain?1.f:reliable?2.f:0.f);
        }
        for(const auto& face:view.elementFaces)triangle(face.points[0],face.points[1],face.points[2],
            face.pending?std::array<float,4>{.42f,.75f,1,.26f}:std::array<float,4>{.46f,.62f,.92f,.17f},0,3);
        for(const auto& edge:view.elementEdges)line(edge.first,edge.second,{.59f,.78f,1,.92f});
        for(const auto& edge:view.pendingEdges)line(edge.first,edge.second,{.44f,.85f,1,1},.002f);
        for(const auto point:view.corners){
            const std::array<V3,6> points{point+V3{.014f,0,0},point-V3{.014f,0,0},point+V3{0,.014f,0},
                point-V3{0,.014f,0},point+V3{0,0,.014f},point-V3{0,0,.014f}};
            for(unsigned x:{0u,1u})for(unsigned y:{2u,3u})for(unsigned z:{4u,5u})
                triangle(points[x],points[y],points[z],{.32f,.59f,.93f,.95f},0,3);
            for(unsigned a=0;a<6;++a)for(unsigned b=a+1;b<6;++b)if(a/2!=b/2)
                line(points[a],points[b],{.85f,.95f,1,.95f},.001f);
        }
    }
};
} // namespace room_native
