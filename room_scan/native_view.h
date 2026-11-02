#pragma once
#include <nlohmann/json.hpp>
#include "native_raster.h"
#include "native_camera.h"
#include "native_surface.h"
#include <unordered_map>
#include <optional>

namespace room_native {
inline constexpr float MiniPanelWidth=.75f;
inline Mat miniPanelPose(){Mat value;value.m[1][3]=-.35f;value.m[2][3]=-1.2f;return value;}
inline std::optional<std::array<float,2>> miniPanelCoordinates(const Mat& head,const Mat& controller){
    const Mat local=inverse(multiply(head,miniPanelPose()));const V3 source=transform(local,position(controller));
    const V3 direction=transform(local,position(controller)+column(controller,2)*(-1))-source;
    if(std::abs(direction.z)<1e-6f)return {};
    const float t=-source.z/direction.z;if(t<=0)return {};
    const V3 point=source+direction*t;
    return std::array<float,2>{(point.x/MiniPanelWidth+.5f)*1024,(.5f-point.y/(MiniPanelWidth*240.f/1024))*240};
}
inline bool hitClose(float x,float y){return x>=936&&x<=992&&y>=24&&y<=80;}
inline void closeButton(Canvas& image,const std::string& hovered=""){
    image.rect(936,24,56,56,hovered=="close"?Color{73,54,62}:Color{28,48,57},12);
    image.line(954,42,974,62,Ink,3);image.line(974,42,954,62,Ink,3);
}
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
        const int alpha=140+int(90*(.5+.5*std::sin(seconds*3)));image.rect(790,38,14,14,{52,224,204,alpha},7);
        image.text(816,30,"LIVE",18,Teal,100);
    }
    closeButton(image,hovered);
    image.text(60,766,"Steam Link Native Passthrough",15,Muted);
}
inline std::string hitButton(const Json& state,float x,float y){
    if(hitClose(x,y))return "close";
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
    struct Face {std::array<V3,3> points;bool pending=false;};
    std::vector<V3> vertices;std::vector<std::array<unsigned,3>> triangles;std::vector<unsigned> quality;
    std::vector<std::pair<V3,V3>> elementEdges;std::vector<std::pair<V3,V3>> pendingEdges;
    std::vector<V3> corners;
    std::vector<Face> elementFaces;
    std::vector<double> firstSeen;
    struct FaceIdentity {std::array<std::uint64_t,3> points;bool operator==(const FaceIdentity& other)const{return points==other.points;}};
    struct FaceHash {std::size_t operator()(const FaceIdentity& face)const{std::size_t seed=0;
        for(auto key:face.points)seed^=std::hash<std::uint64_t>{}(key)+0x9e3779b9+(seed<<6)+(seed>>2);
        return seed;}};
    std::vector<double> faceFirstSeen;
    std::unordered_map<FaceIdentity,double,FaceHash> faceHistory;
    std::vector<float> depthBuffer;
    std::unordered_map<std::uint64_t,double> seen;
    std::uint64_t revision=UINT64_MAX;bool allowed=false;double changed=0;
    void edges(const Json& e,std::vector<std::pair<V3,V3>>& result){
        const auto transform=elementPose(e.at("pose"));const auto& g=e.at("geometry");const auto& boundary=g.at("boundary_xy");
        const bool pending=&result==&pendingEdges;std::vector<V3> polygon;
        for(std::size_t i=0;i<boundary.size();++i){const auto& a=boundary[i];const auto& b=boundary[(i+1)%boundary.size()];
            const V3 p=room_native::transform(transform,{a.at(0),a.at(1),0});polygon.push_back(p);
            result.emplace_back(p,room_native::transform(transform,{b.at(0),b.at(1),0}));}
        for(std::size_t i=1;i+1<polygon.size();++i)elementFaces.push_back({{polygon[0],polygon[i],polygon[i+1]},pending});
        if(!g.at("box_size").is_null()){
            const auto size=vectorJson(g.at("box_size")),center=vectorJson(g.at("box_center"));std::array<V3,8> points{};
            for(unsigned i=0;i<8;++i)points[i]=room_native::transform(transform,center+V3{(i&1?1.f:-1.f)*size.x/2,(i&2?1.f:-1.f)*size.y/2,(i&4?1.f:-1.f)*size.z/2});
            for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit))result.emplace_back(points[i],points[i|bit]);
            for(unsigned axis:{1u,2u,4u})for(unsigned side:{0u,axis}){
                const unsigned a=axis==1?2:1,b=axis==4?2:4;
                elementFaces.push_back({{points[side],points[side|a],points[side|a|b]},pending});
                elementFaces.push_back({{points[side],points[side|a|b],points[side|b]},pending});
            }
        }
    }
    void update(const Json& state,double now){
        const auto next=state.value("world_revision",std::uint64_t(0));
        const auto& room=state.at("room");const bool permitted=!room.is_null()&&room.value("world_visuals_allowed",false);
        if(next==revision&&permitted==allowed)return;
        revision=next;allowed=permitted;changed=now;vertices.clear();triangles.clear();quality.clear();elementEdges.clear();pendingEdges.clear();elementFaces.clear();corners.clear();firstSeen.clear();
        faceFirstSeen.clear();
        if(!allowed){seen.clear();faceHistory.clear();return;}
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
            // New connectivity is new visible coverage even when all three
            // corners existed earlier. Vertex ages alone made such faces pop
            // in already white instead of showing their reconstruction.
            std::unordered_map<FaceIdentity,double,FaceHash> currentFaces;currentFaces.reserve(triangles.size());
            const auto pointKey=[](V3 p){const auto q=[](float v){return std::uint64_t(std::clamp(int(std::floor(v/.01f))+1000000,0,2097151));};
                return (q(p.x)<<42)|(q(p.y)<<21)|q(p.z);};
            for(const auto& triangle:triangles){FaceIdentity key{{pointKey(vertices[triangle[0]]),pointKey(vertices[triangle[1]]),pointKey(vertices[triangle[2]])}};
                std::sort(key.points.begin(),key.points.end());const auto found=faceHistory.find(key);
                const double time=found==faceHistory.end()?now:found->second;currentFaces.emplace(key,time);faceFirstSeen.push_back(time);}
            faceHistory=std::move(currentFaces);
            for(const auto& q:m.at("quality"))quality.push_back(q=="reliable"?2:q=="uncertain"?1:0);
            if(quality.size()!=vertices.size())throw std::runtime_error("Mesh quality extent differs");
        }
        for(const auto& e:room.at("elements"))edges(e,elementEdges);
        if(!state.at("pending").is_null())edges(state.at("pending"),pendingEdges);
        for(const auto& p:state.at("corners"))corners.push_back(vectorJson(p));
    }
    void render(Canvas& image,const Mat& eye,const Frustum& f,double now){
        image.clear();if(!allowed)return;const auto view=inverse(eye);
        std::vector<SurfacePoint> projected(vertices.size());SurfaceCanvas surface(image,depthBuffer);
        for(std::size_t i=0;i<vertices.size();++i){const V3 local=transform(view,vertices[i]);auto& p=projected[i];p.room=vertices[i];p.z=-local.z;
            p.valid=project(local,f,image.width,image.height,p.x,p.y);}
        for(const auto& t:triangles){if(!projected[t[0]].valid||!projected[t[1]].valid||!projected[t[2]].valid)continue;
            const float reveal=float(std::clamp((now-std::max({firstSeen[t[0]],firstSeen[t[1]],firstSeen[t[2]]}))/.35,0.,1.));
            const bool uncertain=quality[t[0]]==1||quality[t[1]]==1||quality[t[2]]==1;
            const bool reliable=quality[t[0]]==2&&quality[t[1]]==2&&quality[t[2]]==2;
            const Color base=uncertain?Color{196,192,174}:Color{178,204,212};
            const auto& a=projected[t[0]];const auto& b=projected[t[1]];const auto& c=projected[t[2]];
            surface.face(a,b,c,{base.r,base.g,base.b,int((reliable?65:uncertain?40:52)*reveal)},uncertain);
        }
        const auto projectedPoint=[&](V3 room){const auto local=transform(view,room);SurfacePoint p;p.room=room;p.z=-local.z;
            p.valid=project(local,f,image.width,image.height,p.x,p.y);return p;};
        for(const auto& face:elementFaces){const auto a=projectedPoint(face.points[0]),b=projectedPoint(face.points[1]),c=projectedPoint(face.points[2]);
            if(a.valid&&b.valid&&c.valid)surface.face(a,b,c,face.pending?Color{107,190,255,65}:Color{118,158,235,42},false,.003f);
        }
        for(const auto& t:triangles){if(!projected[t[0]].valid||!projected[t[1]].valid||!projected[t[2]].valid)continue;
            const bool uncertain=quality[t[0]]==1||quality[t[1]]==1||quality[t[2]]==1;
            const bool reliable=quality[t[0]]==2&&quality[t[1]]==2&&quality[t[2]]==2;
            const float age=float(now-std::max({firstSeen[t[0]],firstSeen[t[1]],firstSeen[t[2]]}));
            const float settled=std::clamp(age/.55f,0.f,1.f),reveal=std::clamp(age/.35f,0.f,1.f);
            const Color base{int(52+(225-52)*settled),int(224+(235-224)*settled),int(204+(240-204)*settled)};
            for(unsigned edge=0;edge<3;++edge){const auto& p=projected[t[edge]];const auto& pp=projected[t[(edge+1)%3]];
                surface.line(p,pp,{base.r,base.g,base.b,int((reliable?180:uncertain?105:145)*reveal)});}
        }
        const auto edge=[&](V3 a,V3 b,Color color,int thickness=3){float x,y,xx,yy;
            if(project(transform(view,a),f,image.width,image.height,x,y)&&project(transform(view,b),f,image.width,image.height,xx,yy))image.line(x,y,xx,yy,color,thickness);};
        for(const auto& [a,b]:elementEdges)edge(a,b,{150,200,255,235},2);
        for(const auto& [a,b]:pendingEdges)edge(a,b,{113,217,255,255},3);
        for(V3 p:corners){const auto point=projectedPoint(p);if(!point.valid)continue;
            const int radius=std::clamp(int(image.width*.018f/point.z),4,18);
            image.rect(int(point.x)-radius,int(point.y)-radius,radius*2,radius*2,{218,241,255,235},radius);
            image.rect(int(point.x)-radius+2,int(point.y)-radius+2,(radius-2)*2,(radius-2)*2,{82,151,238,235},radius-2);}
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
