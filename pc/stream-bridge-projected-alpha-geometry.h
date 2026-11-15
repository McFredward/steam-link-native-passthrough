#pragma once
#include "stream-bridge-projected-alpha-replay.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d11_3.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace stream_bridge::sender::projected_alpha {
// Bounded public creation/Map metadata, not guessed native object offsets.
// Only retained small vertex buffers and reflected register0 float2 layouts are
// admitted. No scene/camera pixels or foreign process memory are read/exported.
class Geometry {
    struct Buffer {
        std::atomic<ID3D11Buffer*> object{nullptr};
        std::array<std::uint8_t,4096> bytes{};UINT size=0;bool valid=false;void* mapped=nullptr;
    };
    struct Layout {
        std::atomic<ID3D11InputLayout*> object{nullptr};UINT slot=0,offset=0;
    };
    std::array<Buffer,16> buffers_{};std::array<Layout,16> layouts_{};
    Buffer* find(ID3D11Resource* object)noexcept{
        for(auto& slot:buffers_)if(slot.object.load(std::memory_order_acquire)==object)return &slot;
        return nullptr;
    }
public:
    Geometry()=default;Geometry(const Geometry&)=delete;Geometry& operator=(const Geometry&)=delete;
    ~Geometry(){for(auto& slot:buffers_)if(auto* object=slot.object.load())object->Release();
        for(auto& slot:layouts_)if(auto* object=slot.object.load())object->Release();}
    // Cold creation callbacks are serialized by the controller. Mutation from
    // native Map/Unmap/Update and draw admission stays on its owning context.
    void buffer(const D3D11_BUFFER_DESC* desc,const D3D11_SUBRESOURCE_DATA* initial,ID3D11Buffer* object)noexcept{
        if(!desc||!object||!(desc->BindFlags&D3D11_BIND_VERTEX_BUFFER)||desc->ByteWidth<32||desc->ByteWidth>4096)return;
        if(find(object))return;
        for(auto& slot:buffers_)if(!slot.object.load(std::memory_order_acquire)){
            slot.size=desc->ByteWidth;slot.valid=initial&&initial->pSysMem;
            if(slot.valid)std::memcpy(slot.bytes.data(),initial->pSysMem,slot.size);
            object->AddRef();slot.object.store(object,std::memory_order_release);return;
        }
    }
    void layout(const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout* object)noexcept{
        if(!elements||!count||count>32||!code||!bytes||bytes>65536||!object)return;
        for(const auto& known:layouts_)if(known.object.load(std::memory_order_acquire)==object)return;
        Reference<ID3D11ShaderReflection> reflection;
        if(FAILED(D3DReflect(code,bytes,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.out()))))return;
        D3D11_SHADER_DESC shader{};
        if(FAILED(reflection->GetDesc(&shader))||shader.InputParameters!=1)return;
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        if(FAILED(reflection->GetInputParameterDesc(0,&input))||input.Register||input.Mask!=3||
            input.ComponentType!=D3D_REGISTER_COMPONENT_FLOAT32||input.SystemValueType!=D3D_NAME_UNDEFINED||!input.SemanticName)return;
        const D3D11_INPUT_ELEMENT_DESC* matched=nullptr;
        for(UINT i=0;i<count;++i){const auto& element=elements[i];
            if(element.SemanticName&&element.SemanticIndex==input.SemanticIndex&&!_stricmp(element.SemanticName,input.SemanticName)){
                if(matched)return;
                matched=&element;
            }
        }
        if(!matched||matched->Format!=DXGI_FORMAT_R32G32_FLOAT||matched->InputSlot>=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT||
            matched->InputSlotClass!=D3D11_INPUT_PER_VERTEX_DATA||matched->InstanceDataStepRate)return;
        UINT elementOffset=matched->AlignedByteOffset;
        if(elementOffset==D3D11_APPEND_ALIGNED_ELEMENT){
            for(auto* prior=elements;prior!=matched;++prior)if(prior->InputSlot==matched->InputSlot)return;
            elementOffset=0;
        }
        for(auto& slot:layouts_)if(!slot.object.load(std::memory_order_acquire)){
            slot.slot=matched->InputSlot;slot.offset=elementOffset;
            object->AddRef();slot.object.store(object,std::memory_order_release);return;
        }
    }
    void beginMap(ID3D11Resource* resource,void* pointer)noexcept{
        if(auto* slot=find(resource)){slot->valid=false;slot->mapped=pointer;}
    }
    void beforeUnmap(ID3D11Resource* resource)noexcept{
        if(auto* slot=find(resource);slot&&slot->mapped){std::memcpy(slot->bytes.data(),slot->mapped,slot->size);slot->mapped=nullptr;slot->valid=true;}
    }
    void update(ID3D11Resource* resource,const void* bytes)noexcept{
        if(auto* slot=find(resource)){slot->valid=bytes!=nullptr;if(bytes)std::memcpy(slot->bytes.data(),bytes,slot->size);slot->mapped=nullptr;}
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{
        for(auto& slot:buffers_)if(!resource||slot.object.load(std::memory_order_acquire)==resource){slot.valid=false;slot.mapped=nullptr;}
    }
    // Caller separately recognizes a VS whose clip position is (2*input.xy-1,
    // 0,1), and a PS without discard. Reproject-X is NOT such a full-height VS.
    // Count4 alone is deliberately insufficient: prove the actual bound layout,
    // vertex bytes/range, rectangle, winding, viewport, scissor and sample mask.
    bool fullUnitQuad(ID3D11DeviceContext* context,UINT count,UINT start,UINT width,UINT height,UINT samples)noexcept{
        if(!context||count!=4||!width||!height||(samples!=1&&samples!=4))return false;
        Reference<ID3D11Predicate> predicate;BOOL predicated=FALSE;context->GetPredication(predicate.out(),&predicated);
        if(predicate.value)return false;
        Reference<ID3D11GeometryShader> geometry;Reference<ID3D11HullShader> hull;Reference<ID3D11DomainShader> domain;
        context->GSGetShader(geometry.out(),nullptr,nullptr);context->HSGetShader(hull.out(),nullptr,nullptr);context->DSGetShader(domain.out(),nullptr,nullptr);
        if(geometry.value||hull.value||domain.value)return false;
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)return false;
        Reference<ID3D11InputLayout> actualLayout;context->IAGetInputLayout(actualLayout.out());
        const Layout* layoutInfo=nullptr;
        for(const auto& known:layouts_)if(known.object.load(std::memory_order_acquire)==actualLayout.value&&actualLayout.value)layoutInfo=&known;
        if(!layoutInfo)return false;
        Reference<ID3D11Buffer> actualBuffer;UINT stride=0,offset=0;
        context->IAGetVertexBuffers(layoutInfo->slot,1,actualBuffer.out(),&stride,&offset);
        auto* bufferInfo=find(actualBuffer.value);if(!bufferInfo||!bufferInfo->valid||stride<8)return false;
        std::array<std::array<float,2>,4> points{};
        for(unsigned i=0;i<4;++i){
            const auto at=std::uint64_t(offset)+(std::uint64_t(start)+i)*stride+layoutInfo->offset;
            if(at>bufferInfo->size||8>bufferInfo->size-at)return false;
            std::memcpy(points[i].data(),bufferInfo->bytes.data()+at,8);
            if(!std::isfinite(points[i][0])||!std::isfinite(points[i][1]))return false;
        }
        float left=points[0][0],right=left,top=points[0][1],bottom=top;
        for(const auto& point:points){left=point[0]<left?point[0]:left;right=point[0]>right?point[0]:right;
            top=point[1]<top?point[1]:top;bottom=point[1]>bottom?point[1]:bottom;}
        if(left>0||right<1||top>0||bottom<1||left==right||top==bottom)return false;
        unsigned corners=0;
        for(const auto& point:points){
            if((point[0]!=left&&point[0]!=right)||(point[1]!=top&&point[1]!=bottom))return false;
            const auto bit=1u<<((point[0]==right?1u:0u)|(point[1]==bottom?2u:0u));
            if(corners&bit)return false;
            corners|=bit;
        }
        if(corners!=15||points[1][0]==points[2][0]||points[1][1]==points[2][1])return false;
        UINT viewports=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&viewports,&viewport);
        if(viewports!=1||viewport.TopLeftX||viewport.TopLeftY||viewport.Width!=float(width)||viewport.Height!=float(height)||
            viewport.MinDepth!=0||viewport.MaxDepth!=1)return false;
        Reference<ID3D11RasterizerState> raster;context->RSGetState(raster.out());
        D3D11_RASTERIZER_DESC desc{};desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_BACK;
        if(raster.value)raster->GetDesc(&desc);
        if(desc.FillMode!=D3D11_FILL_SOLID)return false;
        if(raster.value){Reference<ID3D11RasterizerState2> latest;
            if(SUCCEEDED(raster->QueryInterface(__uuidof(ID3D11RasterizerState2),reinterpret_cast<void**>(latest.out())))){
                D3D11_RASTERIZER_DESC2 desc2{};latest->GetDesc2(&desc2);
                if(desc2.ForcedSampleCount||desc2.ConservativeRaster!=D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF)return false;
            }
            Reference<ID3D11RasterizerState1> extended;
            if(SUCCEEDED(raster->QueryInterface(__uuidof(ID3D11RasterizerState1),reinterpret_cast<void**>(extended.out())))){
                D3D11_RASTERIZER_DESC1 desc1{};extended->GetDesc1(&desc1);if(desc1.ForcedSampleCount)return false;
            }
        }
        const float area=(points[1][0]-points[0][0])*(points[2][1]-points[0][1])-
            (points[1][1]-points[0][1])*(points[2][0]-points[0][0]);
        const bool front=desc.FrontCounterClockwise?area>0:area<0;
        if((desc.CullMode==D3D11_CULL_BACK&&!front)||(desc.CullMode==D3D11_CULL_FRONT&&front))return false;
        if(desc.ScissorEnable){UINT rectangles=1;D3D11_RECT rect{};context->RSGetScissorRects(&rectangles,&rect);
            if(rectangles!=1||rect.left>0||rect.top>0||rect.right<LONG(width)||rect.bottom<LONG(height))return false;}
        Reference<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask=0;context->OMGetBlendState(blend.out(),factors,&mask);
        return (mask&((1u<<samples)-1))==((1u<<samples)-1);
    }
};
} // namespace stream_bridge::sender::projected_alpha
