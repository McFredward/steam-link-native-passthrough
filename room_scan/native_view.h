#pragma once
#include <nlohmann/json.hpp>
#include "native_raster.h"
#include "native_camera.h"
#include <unordered_map>
#include <optional>

namespace room_native {
inline void dashboard(Canvas& image,const Json& state,double seconds,const std::string& hovered=""){
    image.clear();image.rect(0,0,image.width,image.height,{12,24,30},24);
    image.rect(0,0,image.width,8,{52,224,204},0);
    image.text(60,34,"ROOM SETUP",18,Teal);
    image.text(58,72,state.value("title","Your room"),42,Ink);
    const int step=state.value("step",0);
    for(int i=0;i<5;++i){const int x=60+i*188;image.rect(x,145,164,4,i<step?Teal:Color{42,65,72},2);}
    image.text(60,173,state.value("subtitle",""),23,Muted,900);
    const auto notice=state.value("notice",std::string{});
    if(!notice.empty()){image.rect(58,255,906,58,{60,48,28},12);image.text(72,264,notice,19,Amber,872);}
    if(state.contains("buttons"))for(const auto& b:state["buttons"]){
        const int x=b.at("x"),y=b.at("y"),w=b.at("w"),h=b.at("h");const bool enabled=b.value("enabled",false),primary=b.value("primary",false);
        const bool hover=hovered==b.value("action","");Color fill=primary?Color{36,210,188}:Color{28,48,57};
        if(hover&&enabled)fill=primary?Color{95,239,218}:Color{43,72,82};
        if(!enabled)fill={23,34,40};
        image.rect(x,y,w,h,fill,14);image.text(x+20,y+12,b.value("label",""),24,
            !enabled?Color{87,107,113}:primary?Color{9,32,35}:Ink,w-36,1);
    }
    if(state.value("capture_collecting",false)){
        const int alpha=140+int(90*(.5+.5*std::sin(seconds*3)));image.rect(858,38,14,14,{52,224,204,alpha},7);
        image.text(884,30,"LIVE",18,Teal,100);
    }
    image.text(60,766,"Steam Link Native Passthrough",15,Muted);
}
inline std::string hitButton(const Json& state,float x,float y){
    if(!state.contains("buttons"))return {};
    for(const auto& b:state["buttons"]){const float bx=b.at("x"),by=b.at("y"),w=b.at("w"),h=b.at("h");
        if(b.value("enabled",false)&&x>=bx&&x<=bx+w&&y>=by&&y<=by+h)return b.value("action","");}
    return {};
}
inline void icon(Canvas& image){
    image.clear();image.rect(0,0,image.width,image.height,{12,28,34},24);
    const Color teal{52,224,204};image.line(22,88,22,42,teal,5);image.line(22,42,63,20,teal,5);
    image.line(63,20,106,42,teal,5);image.line(106,42,106,88,teal,5);image.line(22,88,64,111,teal,5);
    image.line(64,111,106,88,teal,5);image.line(64,66,64,111,teal,4);image.line(22,42,64,66,teal,4);image.line(64,66,106,42,teal,4);
}
inline Mat elementPose(const Json& value){const auto& p=value.at("position");const auto& q=value.at("orientation_xyzw");
    return pose({p.at(0),p.at(1),p.at(2)},std::array<float,4>{q.at(0),q.at(1),q.at(2),q.at(3)});}
struct MeshView {
    std::vector<V3> vertices;std::vector<std::array<unsigned,3>> triangles;std::vector<unsigned> quality;
    std::vector<std::pair<V3,V3>> elementEdges;std::vector<std::pair<V3,V3>> pendingEdges;
    std::vector<V3> corners;
    std::vector<double> firstSeen;
    std::unordered_map<std::uint64_t,double> seen;
    std::uint64_t revision=UINT64_MAX;bool allowed=false;double changed=0;
    void edges(const Json& e,std::vector<std::pair<V3,V3>>& result){
        const auto transform=elementPose(e.at("pose"));const auto& g=e.at("geometry");const auto& boundary=g.at("boundary_xy");
        for(std::size_t i=0;i<boundary.size();++i){const auto& a=boundary[i];const auto& b=boundary[(i+1)%boundary.size()];
            result.emplace_back(room_native::transform(transform,{a.at(0),a.at(1),0}),room_native::transform(transform,{b.at(0),b.at(1),0}));}
        if(!g.at("box_size").is_null()){
            const auto size=vectorJson(g.at("box_size")),center=vectorJson(g.at("box_center"));std::array<V3,8> points{};
            for(unsigned i=0;i<8;++i)points[i]=room_native::transform(transform,center+V3{(i&1?1.f:-1.f)*size.x/2,(i&2?1.f:-1.f)*size.y/2,(i&4?1.f:-1.f)*size.z/2});
            for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit))result.emplace_back(points[i],points[i|bit]);
        }
    }
    void update(const Json& state,double now){
        const auto next=state.value("world_revision",std::uint64_t(0));
        const auto& room=state.at("room");const bool permitted=!room.is_null()&&room.value("world_visuals_allowed",false);
        if(next==revision&&permitted==allowed)return;
        revision=next;allowed=permitted;changed=now;vertices.clear();triangles.clear();quality.clear();elementEdges.clear();pendingEdges.clear();corners.clear();firstSeen.clear();
        if(!allowed){seen.clear();return;}
        if(!room.at("mesh").is_null()){
            const auto& m=room.at("mesh");if(m.at("vertices").size()>100000||m.at("triangles").size()>200000)throw std::runtime_error("Mesh exceeds rendering budget");
            for(const auto& v:m.at("vertices"))vertices.push_back(vectorJson(v));
            std::unordered_map<std::uint64_t,double> current;current.reserve(vertices.size());
            for(const auto& p:vertices){const auto quant=[](float v){return std::uint64_t(std::clamp(int(std::floor(v/.03f))+1000000,0,2097151));};
                const std::uint64_t key=(quant(p.x)<<42)|(quant(p.y)<<21)|quant(p.z);
                const auto found=seen.find(key);const double time=found==seen.end()?now:found->second;current[key]=time;firstSeen.push_back(time);}
            seen=std::move(current);
            for(const auto& t:m.at("triangles")){std::array<unsigned,3> triangle{t.at(0),t.at(1),t.at(2)};
                for(unsigned i:triangle)if(i>=vertices.size())throw std::runtime_error("Mesh index outside extent");
                triangles.push_back(triangle);}
            for(const auto& q:m.at("quality"))quality.push_back(q=="reliable"?2:q=="uncertain"?1:0);
            if(quality.size()!=vertices.size())throw std::runtime_error("Mesh quality extent differs");
        }
        for(const auto& e:room.at("elements"))edges(e,elementEdges);
        if(!state.at("pending").is_null())edges(state.at("pending"),pendingEdges);
        for(const auto& p:state.at("corners"))corners.push_back(vectorJson(p));
    }
    void render(Canvas& image,const Mat& eye,const Frustum& f,double now){
        image.clear();if(!allowed)return;const auto view=inverse(eye);
        struct Point {float x=0,y=0;bool valid=false;};std::vector<Point> projected(vertices.size());
        for(std::size_t i=0;i<vertices.size();++i)projected[i].valid=project(transform(view,vertices[i]),f,image.width,image.height,projected[i].x,projected[i].y);
        for(const auto& t:triangles){if(!projected[t[0]].valid||!projected[t[1]].valid||!projected[t[2]].valid)continue;
            const int reveal=int(std::clamp((now-std::max({firstSeen[t[0]],firstSeen[t[1]],firstSeen[t[2]]}))/.25,0.,1.)*90);
            const unsigned q=std::min({quality[t[0]],quality[t[1]],quality[t[2]]});const Color base=q==1?Amber:Teal;
            const auto& a=projected[t[0]];const auto& b=projected[t[1]];const auto& c=projected[t[2]];
            image.triangle(a.x,a.y,b.x,b.y,c.x,c.y,{base.r,base.g,base.b,q==2?12:5});
            for(unsigned edge=0;edge<3;++edge){const auto& p=projected[t[edge]];const auto& pp=projected[t[(edge+1)%3]];
                image.line(p.x,p.y,pp.x,pp.y,{base.r,base.g,base.b,(q==2?130:55)+reveal},1);}
            if(q==1)image.line((a.x+b.x)/2,(a.y+b.y)/2,(a.x+c.x)/2,(a.y+c.y)/2,{255,192,88,150});
        }
        const auto edge=[&](V3 a,V3 b,Color color,int thickness=3){float x,y,xx,yy;
            if(project(transform(view,a),f,image.width,image.height,x,y)&&project(transform(view,b),f,image.width,image.height,xx,yy))image.line(x,y,xx,yy,color,thickness);};
        for(const auto& [a,b]:elementEdges)edge(a,b,{94,237,220,240});
        for(const auto& [a,b]:pendingEdges)edge(a,b,{172,255,115,255},4);
        for(V3 p:corners){for(const V3 axis:{V3{.015f,0,0},V3{0,.015f,0},V3{0,0,.015f}})edge(p-axis,p+axis,{255,255,255,255},4);}
    }
    std::optional<V3> rayHit(V3 source,V3 direction)const{
        float nearest=6;std::optional<V3> hit;
        for(const auto& t:triangles){if(quality[t[0]]!=2||quality[t[1]]!=2||quality[t[2]]!=2)continue;
            const V3 a=vertices[t[0]],ab=vertices[t[1]]-a,ac=vertices[t[2]]-a,h=cross(direction,ac),offset=source-a;
            const float determinant=dot(ab,h);if(std::abs(determinant)<1e-8f)continue;
            const float u=dot(offset,h)/determinant;const V3 q=cross(offset,ab);const float v=dot(direction,q)/determinant,d=dot(ac,q)/determinant;
            if(u>=0&&v>=0&&u+v<=1&&d>0&&d<nearest){nearest=d;hit=source+direction*d;}
        }
        return hit;
    }
};
inline void pointer(Canvas& image,const Mat& eye,const Frustum& f,V3 origin,V3 target,bool marker){
    const Mat view=inverse(eye);float x,y,xx,yy;
    if(!marker&&project(transform(view,origin),f,image.width,image.height,x,y)&&project(transform(view,target),f,image.width,image.height,xx,yy))
        image.line(x,y,xx,yy,{180,245,238,120},2);
    if(marker)for(V3 axis:{V3{.012f,0,0},V3{0,.012f,0},V3{0,0,.012f}}){
        if(project(transform(view,target-axis),f,image.width,image.height,x,y)&&project(transform(view,target+axis),f,image.width,image.height,xx,yy))
            image.line(x,y,xx,yy,{230,255,245,255},3);}
}
} // namespace room_native
