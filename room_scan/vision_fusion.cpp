// Copyright (c) 2018-2024 www.open3d.org
// SPDX-License-Identifier: MIT
// Derived from Open3D 0.19.0 ScalableTSDFVolume/UniformTSDFVolume.
// Modified for Room Setup: color-free standard containers, fixed budgets,
// staged atomic exposures, XR camera axes and separate observation quality.
// See packaging/room-open3d-kernel.json and licenses/Open3D.txt.
#include "vision_common.h"
#include "vendor/open3d/MarchingCubesConst.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace room_vision {
namespace {
constexpr int Resolution=16,BlockVoxels=Resolution*Resolution*Resolution;
using Point=std::array<double,3>;
using Key=std::array<int,3>;
using Edge=std::array<int,4>;
struct Hash {
    template<std::size_t N>std::size_t operator()(const std::array<int,N>& value)const noexcept{
        std::size_t seed=0;for(int v:value)seed^=std::hash<int>{}(v)+0x9e3779b9+(seed<<6)+(seed>>2);return seed;
    }
};
struct Voxel {
    float tsdf=0,weight=0,confidenceSum=0,maxDifference=0;
    std::array<uint16_t,3> views{};
};
static_assert(sizeof(Voxel)<=24);
struct Block {std::array<Voxel,BlockVoxels> voxels{};};
using Blocks=std::unordered_map<Key,std::unique_ptr<Block>,Hash>;
struct Mesh {
    std::vector<std::array<float,3>> vertices;
    std::vector<std::array<uint32_t,3>> triangles;
    std::vector<uint8_t> quality;
};
double distance(const Point& a,const Point& b){double sum=0;for(unsigned k=0;k<3;++k)sum+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(sum);}
bool rigid(const double* matrix){
    if(!matrix)return false;
    for(unsigned i=0;i<12;++i)if(!std::isfinite(matrix[i])||std::abs(matrix[i])>1000)return false;
    for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){double dot=0;
        for(unsigned r=0;r<3;++r)dot+=matrix[r*4+a]*matrix[r*4+b];
        if(std::abs(dot-(a==b?1.:0.))>1e-4)return false;}
    const double determinant=matrix[0]*(matrix[5]*matrix[10]-matrix[6]*matrix[9])-matrix[1]*(matrix[4]*matrix[10]-matrix[6]*matrix[8])+matrix[2]*(matrix[4]*matrix[9]-matrix[5]*matrix[8]);
    return determinant>.9999;
}
class Fusion {
    double voxel_,truncation_,blockLength_;uint32_t maxBlocks_,maxVertices_,maxTriangles_;
    Blocks blocks_;Mesh mesh_;std::vector<Point> viewpoints_;uint64_t sequence_=0;
    Key locate(const Point& p)const{return {int(std::floor(p[0]/blockLength_)),int(std::floor(p[1]/blockLength_)),int(std::floor(p[2]/blockLength_))};}
    static std::size_t index(int x,int y,int z){return std::size_t(x)*Resolution*Resolution+y*Resolution+z;}
    const Block* block(const Blocks& changes,const Key& key)const{
        const auto updated=changes.find(key);if(updated!=changes.end())return updated->second.get();
        const auto existing=blocks_.find(key);return existing==blocks_.end()?nullptr:existing->second.get();
    }
    static uint8_t quality(const Voxel& v){
        if(v.confidenceSum/v.weight<.7f||v.maxDifference>.04f)return 1;
        return v.views[2]?2:0;
    }
    Mesh extract(const Blocks& changes,const std::vector<Key>& keys)const{
        Mesh mesh;std::unordered_map<Edge,uint32_t,Hash> edgeindex_to_vertexindex;
        const double half_voxel_length=voxel_*.5;
        for(const auto& key:keys){const Block* volume0=block(changes,key);require(volume0,"TSDF block unavailable");
            // Cache neighboring units once rather than hash every voxel corner.
            std::array<const Block*,8> neighbors{};
            for(unsigned i=0;i<8;++i)neighbors[i]=block(changes,{key[0]+int(i&1),key[1]+int((i>>1)&1),key[2]+int((i>>2)&1)});
            for(int x=0;x<Resolution;++x)for(int y=0;y<Resolution;++y)for(int z=0;z<Resolution;++z){
                if(volume0->voxels[index(x,y,z)].weight==0)continue;
                int cube_index=0;std::array<float,8> f{};std::array<const Voxel*,8> evidence{};
                for(unsigned i=0;i<8;++i){int a=x+shift[i][0],b=y+shift[i][1],c=z+shift[i][2];
                    const unsigned next=unsigned(a>=Resolution)|unsigned(b>=Resolution)<<1|unsigned(c>=Resolution)<<2;
                    if(!neighbors[next]){cube_index=0;break;}
                    const auto& v=neighbors[next]->voxels[index(a%Resolution,b%Resolution,c%Resolution)];
                    if(v.weight==0){cube_index=0;break;}
                    evidence[i]=&v;f[i]=v.tsdf;if(f[i]<0)cube_index|=1<<i;
                }
                if(cube_index==0||cube_index==255)continue;
                std::array<uint32_t,12> edge_to_index{};
                for(unsigned i=0;i<12;++i)if(edge_table[cube_index]&(1<<i)){
                    Edge edge_index{key[0]*Resolution+x+edge_shift[i][0],key[1]*Resolution+y+edge_shift[i][1],
                        key[2]*Resolution+z+edge_shift[i][2],edge_shift[i][3]};
                    const auto found=edgeindex_to_vertexindex.find(edge_index);
                    const auto a=unsigned(edge_to_vert[i][0]),b=unsigned(edge_to_vert[i][1]);
                    const uint8_t qa=quality(*evidence[a]),qb=quality(*evidence[b]);
                    const uint8_t q=qa==1||qb==1?1:qa==2&&qb==2?2:0;
                    if(found==edgeindex_to_vertexindex.end()){
                        require(mesh.vertices.size()<maxVertices_,"Scan vertex budget reached");
                        Point pt{half_voxel_length+voxel_*edge_index[0],half_voxel_length+voxel_*edge_index[1],half_voxel_length+voxel_*edge_index[2]};
                        const double f0=std::abs(double(f[a])),f1=std::abs(double(f[b]));
                        pt[unsigned(edge_index[3])]+=f0*voxel_/(f0+f1);
                        for(double coordinate:pt)require(std::isfinite(coordinate)&&std::abs(coordinate)<=1000,"TSDF mesh outside room limits");
                        const auto vertex=uint32_t(mesh.vertices.size());edge_to_index[i]=vertex;edgeindex_to_vertexindex.emplace(edge_index,vertex);
                        mesh.vertices.push_back({float(pt[0]),float(pt[1]),float(pt[2])});mesh.quality.push_back(q);
                    }else{edge_to_index[i]=found->second;
                        auto& old=mesh.quality[found->second];old=old==1||q==1?1:old==2&&q==2?2:0;}
                }
                for(unsigned i=0;tri_table[cube_index][i]!=-1;i+=3){
                    const std::array<uint32_t,3> triangle{edge_to_index[unsigned(tri_table[cube_index][i])],
                        edge_to_index[unsigned(tri_table[cube_index][i+2])],edge_to_index[unsigned(tri_table[cube_index][i+1])]};
                    // Marching cubes can produce vanishing faces at a zero
                    // crossing. Apply the room format's area bound before the
                    // exposure is committed, using its actual float vertices.
                    const auto& a=mesh.vertices[triangle[0]];const auto& b=mesh.vertices[triangle[1]];const auto& c=mesh.vertices[triangle[2]];
                    const std::array<double,3> ab{double(b[0])-a[0],double(b[1])-a[1],double(b[2])-a[2]},
                        ac{double(c[0])-a[0],double(c[1])-a[1],double(c[2])-a[2]};
                    const std::array<double,3> normal{ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]};
                    if(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]<=1e-14)continue;
                    require(mesh.triangles.size()<maxTriangles_,"Scan triangle budget reached");
                    mesh.triangles.push_back(triangle);
                }
            }
        }
        return mesh;
    }
public:
    Fusion(float voxel,uint32_t maxBlocks,uint32_t maxVertices,uint32_t maxTriangles)
        :voxel_(voxel),truncation_(3*voxel_),blockLength_(Resolution*voxel_),maxBlocks_(maxBlocks),maxVertices_(maxVertices),maxTriangles_(maxTriangles){
        require(std::isfinite(voxel)&&voxel>=.01f&&voxel<=.1f&&maxBlocks>=1&&maxBlocks<=2048
            &&maxVertices>=1&&maxVertices<=100000&&maxTriangles>=1&&maxTriangles<=200000,"Invalid TSDF budgets");
        viewpoints_.reserve(60000);
    }
    uint32_t blockCount()const{return uint32_t(blocks_.size());}
    void rebind(){sequence_=0;}
    void meshBudget(uint32_t vertices,uint32_t triangles){
        require(vertices>=1&&vertices<=100000&&triangles>=1&&triangles<=200000
            &&mesh_.vertices.size()<=vertices&&mesh_.triangles.size()<=triangles,"Aligned geometry budget reached");
        maxVertices_=vertices;maxTriangles_=triangles;
    }
    const Mesh& mesh()const{return mesh_;}
    int integrate(uint32_t width,uint32_t height,double fx,double fy,double cx,double cy,
                    const float* depth,const float* confidence,const double* roomFromCamera,uint64_t sequence){
        require(width>=2&&width<=160&&height>=2&&height<=120&&depth&&confidence&&rigid(roomFromCamera),"Invalid TSDF observation");
        require(std::isfinite(fx)&&fx>0&&fx<=100000&&std::isfinite(fy)&&fy>0&&fy<=100000&&std::isfinite(cx)&&cx>=0&&cx<width
            &&std::isfinite(cy)&&cy>=0&&cy<height,"Invalid TSDF intrinsics");
        require(sequence>sequence_&&sequence<(uint64_t(1)<<63),"Stale TSDF exposure");
        require(viewpoints_.size()<60000,"Scan exposure budget reached");
        const Point viewpoint{roomFromCamera[3],roomFromCamera[7],roomFromCamera[11]};
        std::vector<float> validDepth(width*height),multiplier(width*height);
        std::unordered_set<Key,Hash> touched;
        int observations=0;
        for(uint32_t v=0;v<height;++v)for(uint32_t u=0;u<width;++u){const auto i=v*width+u;
            const float d=depth[i],certainty=confidence[i];
            require(std::isfinite(d)&&d>=0&&d<=100&&std::isfinite(certainty)&&certainty>=0&&certainty<=1,"Malformed TSDF sample");
            const double a=(u-cx)/fx,b=(v-cy)/fy;multiplier[i]=float(std::sqrt(1+a*a+b*b));
            if(d<.25f||d>6||certainty<.4f)continue;
            validDepth[i]=d;++observations;
            const Point camera{a*d,-b*d,-d};Point point{};
            for(unsigned r=0;r<3;++r){point[r]=roomFromCamera[r*4+3];for(unsigned c=0;c<3;++c)point[r]+=roomFromCamera[r*4+c]*camera[c];
                require(std::isfinite(point[r])&&std::abs(point[r])<=1000,"Depth outside room limits");}
            const auto lo=locate({point[0]-truncation_,point[1]-truncation_,point[2]-truncation_});
            const auto hi=locate({point[0]+truncation_,point[1]+truncation_,point[2]+truncation_});
            for(int x=lo[0];x<=hi[0];++x)for(int y=lo[1];y<=hi[1];++y)for(int z=lo[2];z<=hi[2];++z){
                touched.insert({x,y,z});require(touched.size()<=maxBlocks_,"Scan volume budget reached");}
        }
        std::vector<Key> allKeys;allKeys.reserve(blocks_.size()+touched.size());
        for(const auto& item:blocks_)allKeys.push_back(item.first);
        for(const auto& key:touched)if(blocks_.find(key)==blocks_.end())allKeys.push_back(key);
        require(allKeys.size()<=maxBlocks_,"Scan volume budget reached");std::sort(allKeys.begin(),allKeys.end());
        // Stage every changed block. Failed capacity/geometry checks leave both
        // the accepted volume and its last published mesh unchanged.
        Blocks changes;changes.reserve(touched.size());
        for(const auto& key:touched){const auto old=blocks_.find(key);
            changes.emplace(key,old==blocks_.end()?std::make_unique<Block>():std::make_unique<Block>(*old->second));}
        std::array<float,12> extrinsic{};
        // inverse(roomFromNativeCamera * diag(1,-1,-1)): optical +Z forward.
        for(unsigned r=0;r<3;++r){const float sign=r? -1.f:1.f;
            for(unsigned c=0;c<3;++c)extrinsic[r*4+c]=sign*float(roomFromCamera[c*4+r]);
            double translation=0;for(unsigned c=0;c<3;++c)translation-=double(sign)*roomFromCamera[c*4+r]*viewpoint[c];
            extrinsic[r*4+3]=float(translation);}
        const auto viewId=uint16_t(viewpoints_.size()+1);
        const float voxel_length_f=float(voxel_),half_voxel_length_f=voxel_length_f*.5f,sdf_trunc_f=float(truncation_),sdf_trunc_inv_f=1/sdf_trunc_f;
        const float safe_width_f=width-.0001f,safe_height_f=height-.0001f;
        for(auto& item:changes){const auto& key=item.first;auto& voxels=item.second->voxels;
            for(int x=0;x<Resolution;++x)for(int y=0;y<Resolution;++y){
                const std::array<float,3> world{float(half_voxel_length_f+voxel_length_f*x+key[0]*blockLength_),
                    float(half_voxel_length_f+voxel_length_f*y+key[1]*blockLength_),float(half_voxel_length_f+key[2]*blockLength_)};
                std::array<float,3> pt_camera{};
                for(unsigned r=0;r<3;++r){pt_camera[r]=extrinsic[r*4+3];for(unsigned c=0;c<3;++c)pt_camera[r]+=extrinsic[r*4+c]*world[c];}
                for(int z=0;z<Resolution;++z,pt_camera[0]+=extrinsic[2]*voxel_length_f,
                    pt_camera[1]+=extrinsic[6]*voxel_length_f,pt_camera[2]+=extrinsic[10]*voxel_length_f){
                    if(pt_camera[2]<=0)continue;
                    const float u_f=pt_camera[0]*float(fx)/pt_camera[2]+float(cx)+.5f;
                    const float v_f=pt_camera[1]*float(fy)/pt_camera[2]+float(cy)+.5f;
                    if(!(u_f>=.0001f&&u_f<safe_width_f&&v_f>=.0001f&&v_f<safe_height_f))continue;
                    const auto pixel=uint32_t(v_f)*width+uint32_t(u_f);const float d=validDepth[pixel];if(d<=0)continue;
                    const float sdf=(d-pt_camera[2])*multiplier[pixel];
                    if(sdf>-sdf_trunc_f){
                        const float tsdf=std::min(1.f,sdf*sdf_trunc_inv_f);auto& value=voxels[index(x,y,z)];
                        // Keep revisited surfaces responsive. An unlimited
                        // running average makes an early mistake increasingly
                        // difficult to correct, and a lifetime maximum residual
                        // prevents even consistent later evidence recovering.
                        if(value.weight>0)value.maxDifference=std::max(value.maxDifference*.85f,std::abs(tsdf-value.tsdf)*sdf_trunc_f);
                        const float oldWeight=std::min(value.weight,15.f);
                        const float oldConfidence=value.weight>0?value.confidenceSum/value.weight:0.f;
                        value.tsdf=(value.tsdf*oldWeight+tsdf)/(oldWeight+1.f);
                        value.weight=oldWeight+1.f;value.confidenceSum=oldConfidence*oldWeight+confidence[pixel];
                        bool distinct=true;for(auto id:value.views)if(id&&distance(viewpoint,viewpoints_[id-1])<.05)distinct=false;
                        if(distinct)for(auto& id:value.views)if(!id){id=viewId;break;}
                    }
                }
            }
        }
        auto mesh=extract(changes,allKeys);
        // Reserve nodes before committing; node transfer and pointer moves do
        // not allocate after this point. Retain exposure identity on failure.
        blocks_.reserve(allKeys.size());
        for(auto it=changes.begin();it!=changes.end();){const auto found=blocks_.find(it->first);
            if(found!=blocks_.end()){found->second=std::move(it->second);++it;}
            else{auto node=changes.extract(it++);blocks_.insert(std::move(node));}}
        mesh_=std::move(mesh);viewpoints_.push_back(viewpoint);sequence_=sequence;return observations;
    }
};
}
}
extern "C" {
void* sfnp_fusion_create(float voxel,uint32_t blocks,uint32_t vertices,uint32_t triangles){void* result=nullptr;
    room_vision::checked([&]{result=new room_vision::Fusion(voxel,blocks,vertices,triangles);return 0;});return result;}
void sfnp_fusion_destroy(void* context){delete static_cast<room_vision::Fusion*>(context);}
int sfnp_fusion_integrate(void* context,uint32_t width,uint32_t height,double fx,double fy,double cx,double cy,
            const float* depth,const float* confidence,const double* pose,uint64_t sequence){
    return room_vision::checked([&]{room_vision::require(context,"TSDF context unavailable");
        return static_cast<room_vision::Fusion*>(context)->integrate(width,height,fx,fy,cx,cy,depth,confidence,pose,sequence);});}
int sfnp_fusion_rebind(void* context){return room_vision::checked([&]{room_vision::require(context,"TSDF context unavailable");static_cast<room_vision::Fusion*>(context)->rebind();return 0;});}
int sfnp_fusion_mesh_budget(void* context,uint32_t vertices,uint32_t triangles){return room_vision::checked([&]{
    room_vision::require(context,"TSDF context unavailable");static_cast<room_vision::Fusion*>(context)->meshBudget(vertices,triangles);return 0;});}
int sfnp_fusion_mesh_size(void* context,uint32_t* vertices,uint32_t* triangles){return room_vision::checked([&]{
    room_vision::require(context&&vertices&&triangles,"TSDF mesh buffers unavailable");const auto& mesh=static_cast<room_vision::Fusion*>(context)->mesh();
    *vertices=uint32_t(mesh.vertices.size());*triangles=uint32_t(mesh.triangles.size());return 0;});}
int sfnp_fusion_copy_mesh(void* context,float* vertices,uint32_t vertexCapacity,uint32_t* triangles,uint32_t triangleCapacity,uint8_t* quality){
    return room_vision::checked([&]{room_vision::require(context,"TSDF context unavailable");const auto& mesh=static_cast<room_vision::Fusion*>(context)->mesh();
        room_vision::require(vertexCapacity>=mesh.vertices.size()&&triangleCapacity>=mesh.triangles.size()
            &&(mesh.vertices.empty()||(vertices&&quality))&&(mesh.triangles.empty()||triangles),"TSDF mesh capacity differs");
        if(!mesh.vertices.empty()){std::memcpy(vertices,mesh.vertices.data(),mesh.vertices.size()*sizeof(mesh.vertices[0]));std::memcpy(quality,mesh.quality.data(),mesh.quality.size());}
        if(!mesh.triangles.empty())std::memcpy(triangles,mesh.triangles.data(),mesh.triangles.size()*sizeof(mesh.triangles[0]));
        return 0;});}
uint32_t sfnp_fusion_block_count(void* context){return context?static_cast<room_vision::Fusion*>(context)->blockCount():0;}
}
