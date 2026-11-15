#pragma once
#include "stream-bridge-projected-alpha-replay.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <d3d11_3.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>

namespace stream_bridge::sender::projected_alpha {
// Bounded public creation/Map metadata, not guessed native object offsets.
// Only retained small vertex buffers and reflected register0 float2 layouts are
// admitted. No scene/camera pixels or foreign process memory are read/exported.
class Geometry {
    struct Buffer {
        inline static constexpr GUID Id{0x74b78be9,0xe9c4,0x44a3,{0x93,0x25,0x76,0x09,0x4e,0x5a,0x51,0x4a}};
        inline static std::atomic<unsigned> live{0};
        static constexpr unsigned Limit=4096;
        std::array<std::uint8_t,4096> bytes{};UINT size=0;
        std::uint64_t epoch=0;bool valid=false,immutable=false;void* mapped=nullptr;
    };
    struct Constants {
        inline static constexpr GUID Id{0x46b562ac,0xcc85,0x462b,{0x8a,0x65,0xb5,0x19,0x2e,0x36,0xf3,0xa4}};
        inline static std::atomic<unsigned> live{0};
        static constexpr unsigned Limit=128;
        std::array<std::uint8_t,65536> bytes{};UINT size=0;
        std::uint64_t epoch=0;bool valid=false;void* mapped=nullptr;
    };
    struct Layout {
        inline static constexpr GUID Id{0x018eb540,0x92d1,0x4610,{0xa6,0xc5,0x81,0x43,0x7e,0x6b,0x32,0x20}};
        inline static std::atomic<unsigned> live{0};
        static constexpr unsigned Limit=4096;
        UINT slot=0,offset=0;unsigned reason=0;
    };
    template<class Data>struct Record final:IUnknown {
        std::atomic<ULONG> references{1};
        const Geometry* owner;std::uint64_t generation;Data data{};
        Record(const Geometry* value,std::uint64_t serial)noexcept:owner(value),generation(serial){}
        ~Record(){Data::live.fetch_sub(1,std::memory_order_relaxed);}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
            if(!out)return E_POINTER;
            *out=nullptr;
            if(iid!=IID_IUnknown&&iid!=Data::Id)return E_NOINTERFACE;
            *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef()override{return references.fetch_add(1,std::memory_order_relaxed)+1;}
        ULONG STDMETHODCALLTYPE Release()override{
            const auto remaining=references.fetch_sub(1,std::memory_order_acq_rel)-1;
            if(!remaining)delete this;
            return remaining;
        }
    };
    inline static std::atomic<std::uint64_t> nextGeneration_{1};
    const std::uint64_t generation_=nextGeneration_.fetch_add(1,std::memory_order_relaxed);
    std::uint64_t writeEpoch_=1;
    // Native objects own these fixed-size records. There are no retained
    // resource references or first-16 tables that can fill with retired UI
    // resources. Allocation is cold only, with hard per-process record limits.
    template<class Data>Reference<Record<Data>> create()noexcept{
        Reference<Record<Data>> out;
        if(!generation_)return out;
        if(Data::live.fetch_add(1,std::memory_order_acq_rel)>=Data::Limit){
            Data::live.fetch_sub(1,std::memory_order_release);return out;
        }
        out.value=new(std::nothrow) Record<Data>(this,generation_);
        if(!out.value)Data::live.fetch_sub(1,std::memory_order_release);
        return out;
    }
    template<class Data>Reference<Record<Data>> get(ID3D11DeviceChild* object)noexcept{
        Reference<Record<Data>> out;Reference<IUnknown> unknown;UINT bytes=sizeof(IUnknown*);
        if(!object||FAILED(object->GetPrivateData(Data::Id,&bytes,unknown.out()))||bytes!=sizeof(IUnknown*)||!unknown.value)return out;
        if(FAILED(unknown->QueryInterface(Data::Id,reinterpret_cast<void**>(out.out()))))return out;
        if(out->owner!=this||out->generation!=generation_){out->Release();out.value=nullptr;}
        return out;
    }
    static UINT floatBytes(DXGI_FORMAT format)noexcept{
        switch(format){
        case DXGI_FORMAT_R32_FLOAT:return 4;
        case DXGI_FORMAT_R32G32_FLOAT:return 8;
        case DXGI_FORMAT_R32G32B32_FLOAT:return 12;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:return 16;
        default:return 0;
        }
    }
    static bool appendedOffset(const D3D11_INPUT_ELEMENT_DESC* elements,const D3D11_INPUT_ELEMENT_DESC* match,UINT& offset)noexcept{
        if(match->AlignedByteOffset!=D3D11_APPEND_ALIGNED_ELEMENT){offset=match->AlignedByteOffset;return offset<=4096;}
        std::array<UINT,D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> ends{};
        for(auto* element=elements;element<=match;++element){
            if(element->InputSlot>=ends.size())return false;
            const auto size=floatBytes(element->Format);if(!size)return false;
            const auto at=element->AlignedByteOffset==D3D11_APPEND_ALIGNED_ELEMENT?
                (ends[element->InputSlot]+3u)&~3u:element->AlignedByteOffset;
            if(at>4096||size>4096-at)return false;
            ends[element->InputSlot]=at+size;
            if(element==match){offset=at;return true;}
        }
        return false;
    }
public:
    Geometry()=default;Geometry(const Geometry&)=delete;Geometry& operator=(const Geometry&)=delete;
    void buffer(const D3D11_BUFFER_DESC* desc,const D3D11_SUBRESOURCE_DATA* initial,ID3D11Buffer* object)noexcept{
        if(desc&&object&&(desc->BindFlags&D3D11_BIND_CONSTANT_BUFFER)&&desc->ByteWidth&&desc->ByteWidth<=65536&&!get<Constants>(object).value){
            auto record=create<Constants>();if(record.value){auto& data=record->data;data.size=desc->ByteWidth;
                data.valid=initial&&initial->pSysMem;data.epoch=writeEpoch_;
                if(data.valid)std::memcpy(data.bytes.data(),initial->pSysMem,data.size);
                (void)object->SetPrivateDataInterface(Constants::Id,record.value);}
        }
        if(!desc||!object||!(desc->BindFlags&D3D11_BIND_VERTEX_BUFFER)||desc->ByteWidth<32||desc->ByteWidth>4096)return;
        if(get<Buffer>(object).value)return;
        auto record=create<Buffer>();if(!record.value)return;
        auto& data=record->data;data.size=desc->ByteWidth;data.valid=initial&&initial->pSysMem;
        data.immutable=desc->Usage==D3D11_USAGE_IMMUTABLE;data.epoch=writeEpoch_;
        if(data.valid)std::memcpy(data.bytes.data(),initial->pSysMem,data.size);
        (void)object->SetPrivateDataInterface(Buffer::Id,record.value);
    }
    void layout(const D3D11_INPUT_ELEMENT_DESC* elements,UINT count,const void* code,SIZE_T bytes,ID3D11InputLayout* object)noexcept{
        if(!object||get<Layout>(object).value)return;
        auto record=create<Layout>();if(!record.value)return;
        auto& data=record->data;
        const auto finish=[&](unsigned reason)noexcept{data.reason=reason;(void)object->SetPrivateDataInterface(Layout::Id,record.value);};
        if(!elements||!count||count>32||!code||!bytes||bytes>65536){finish(101);return;}
        Reference<ID3D11ShaderReflection> reflection;
        if(FAILED(D3DReflect(code,bytes,IID_ID3D11ShaderReflection,reinterpret_cast<void**>(reflection.out())))){finish(102);return;}
        D3D11_SHADER_DESC shader{};
        if(FAILED(reflection->GetDesc(&shader))||!shader.InputParameters||shader.InputParameters>32){finish(103);return;}
        // The bound reviewed VS reads register0.xy. Native layouts may supply
        // float3/float4 positions or be created with a wider generic signature;
        // the unused components do not change those first two float values.
        D3D11_SIGNATURE_PARAMETER_DESC input{};bool found=false;
        for(UINT i=0;i<shader.InputParameters;++i){
            D3D11_SIGNATURE_PARAMETER_DESC candidate{};
            if(FAILED(reflection->GetInputParameterDesc(i,&candidate))){finish(104);return;}
            if(candidate.Register||(candidate.Mask&3)!=3)continue;
            if(found){finish(104);return;}
            input=candidate;found=true;
        }
        if(!found||input.ComponentType!=D3D_REGISTER_COMPONENT_FLOAT32||input.SystemValueType!=D3D_NAME_UNDEFINED||!input.SemanticName){finish(104);return;}
        const D3D11_INPUT_ELEMENT_DESC* matched=nullptr;
        for(UINT i=0;i<count;++i){const auto& element=elements[i];
            if(element.SemanticName&&element.SemanticIndex==input.SemanticIndex&&!_stricmp(element.SemanticName,input.SemanticName)){
                if(matched){finish(105);return;}
                matched=&element;
            }
        }
        if(!matched){finish(105);return;}
        if(floatBytes(matched->Format)<8){finish(106);return;}
        if(matched->InputSlot>=D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT||matched->InputSlotClass!=D3D11_INPUT_PER_VERTEX_DATA||matched->InstanceDataStepRate){finish(107);return;}
        if(!appendedOffset(elements,matched,data.offset)){finish(108);return;}
        data.slot=matched->InputSlot;finish(0);
    }
    void beginMap(ID3D11Resource* resource,void* pointer)noexcept{
        auto record=get<Buffer>(resource);if(record.value){record->data.valid=false;record->data.mapped=pointer;}
        auto constants=get<Constants>(resource);if(constants.value){constants->data.valid=false;constants->data.mapped=pointer;}
    }
    void beforeUnmap(ID3D11Resource* resource)noexcept{
        const auto finish=[this](auto& record)noexcept{if(!record.value)return;auto& data=record->data;
            if(data.mapped){std::memcpy(data.bytes.data(),data.mapped,data.size);data.mapped=nullptr;data.valid=true;data.epoch=writeEpoch_;}};
        auto record=get<Buffer>(resource);finish(record);auto constants=get<Constants>(resource);finish(constants);
    }
    void update(ID3D11Resource* resource,const void* bytes)noexcept{
        const auto update=[this,bytes](auto& record)noexcept{if(!record.value)return;auto& data=record->data;
            data.valid=bytes!=nullptr;if(bytes)std::memcpy(data.bytes.data(),bytes,data.size);data.mapped=nullptr;data.epoch=writeEpoch_;};
        auto record=get<Buffer>(resource);update(record);auto constants=get<Constants>(resource);update(constants);
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{
        if(!resource){if(writeEpoch_)++writeEpoch_;return;}
        auto record=get<Buffer>(resource);if(record.value){record->data.valid=false;record->data.mapped=nullptr;}
        auto constants=get<Constants>(resource);if(constants.value){constants->data.valid=false;constants->data.mapped=nullptr;}
    }
    // Current logical public b0 range only. No GPU readback or guessed native
    // constant offsets; unknown, copied, UAV-written or stale bytes refuse.
    bool constantPrefix(ID3D11DeviceContext* context,void* output,UINT bytes)noexcept{
        if(!context||!output||!bytes||bytes>80)return false;
        Reference<ID3D11DeviceContext1> extended;
        if(FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))))return false;
        Reference<ID3D11Buffer> buffer;UINT offset=0,count=0;
        extended->PSGetConstantBuffers1(0,1,buffer.out(),&offset,&count);
        auto record=get<Constants>(buffer.value);if(!record.value)return false;const auto& data=record->data;
        if(!data.valid||!writeEpoch_||data.epoch!=writeEpoch_||offset>data.size/16||count<((bytes+15)/16)||bytes>data.size-offset*16)return false;
        std::memcpy(output,data.bytes.data()+offset*16,bytes);return true;
    }
    // Return a completely drawn integer viewport inside a larger allocation.
    // This establishes only that rectangle, never the untouched padding.
    bool viewportQuad(ID3D11DeviceContext* context,UINT count,UINT start,UINT width,UINT height,UINT samples,
        UINT& coveredWidth,UINT& coveredHeight)noexcept{
        if(!context)return false;
        std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> values{};
        UINT n=UINT(values.size());context->RSGetViewports(&n,values.data());const auto& v=values[0];
        if(n!=1||v.TopLeftX||v.TopLeftY||!std::isfinite(v.Width)||!std::isfinite(v.Height)||
            v.Width<1||v.Height<1||v.Width>float(width)||v.Height>float(height)||
            v.Width!=std::floor(v.Width)||v.Height!=std::floor(v.Height))return false;
        const UINT w=UINT(v.Width),h=UINT(v.Height);
        if(!fullUnitQuad(context,count,start,w,h,samples))return false;
        coveredWidth=w;coveredHeight=h;return true;
    }
    // Caller separately recognizes a VS whose clip position is (2*input.xy-1,
    // 0,1), and a PS without discard. Reproject-X is NOT such a full-height VS.
    // Count4 alone is deliberately insufficient: prove the actual bound layout,
    // vertex bytes/range, rectangle, winding, viewport, scissor and sample mask.
    bool fullUnitQuad(ID3D11DeviceContext* context,UINT count,UINT start,UINT width,UINT height,UINT samples,unsigned* reason=nullptr)noexcept{
        if(reason)*reason=0;
        const auto refuse=[reason](unsigned value)noexcept{if(reason)*reason=value;return false;};
        if(!context||count!=4||!width||!height||(samples!=1&&samples!=4))return refuse(1);
        Reference<ID3D11Predicate> predicate;BOOL predicated=FALSE;context->GetPredication(predicate.out(),&predicated);
        if(predicate.value)return refuse(2);
        Reference<ID3D11GeometryShader> geometry;Reference<ID3D11HullShader> hull;Reference<ID3D11DomainShader> domain;
        context->GSGetShader(geometry.out(),nullptr,nullptr);context->HSGetShader(hull.out(),nullptr,nullptr);context->DSGetShader(domain.out(),nullptr,nullptr);
        if(geometry.value||hull.value||domain.value)return refuse(3);
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        if(topology!=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)return refuse(4);
        Reference<ID3D11InputLayout> actualLayout;context->IAGetInputLayout(actualLayout.out());
        auto layoutRecord=get<Layout>(actualLayout.value);
        const auto* layoutInfo=layoutRecord.value?&layoutRecord->data:nullptr;
        if(!layoutInfo)return refuse(5);
        if(layoutInfo->reason)return refuse(layoutInfo->reason);
        Reference<ID3D11Buffer> actualBuffer;UINT stride=0,offset=0;
        context->IAGetVertexBuffers(layoutInfo->slot,1,actualBuffer.out(),&stride,&offset);
        auto bufferRecord=get<Buffer>(actualBuffer.value);const auto* bufferInfo=bufferRecord.value?&bufferRecord->data:nullptr;
        if(!bufferInfo)return refuse(6);
        if(!bufferInfo->valid||(!bufferInfo->immutable&&(!writeEpoch_||bufferInfo->epoch!=writeEpoch_)))return refuse(7);
        if(stride<8)return refuse(8);
        std::array<std::array<float,2>,4> points{};
        for(unsigned i=0;i<4;++i){
            if(std::uint64_t(start)+i>bufferInfo->size/stride)return refuse(9);
            const auto at=std::uint64_t(offset)+(std::uint64_t(start)+i)*stride+layoutInfo->offset;
            if(at>bufferInfo->size||8>bufferInfo->size-at)return refuse(9);
            std::memcpy(points[i].data(),bufferInfo->bytes.data()+at,8);
            if(!std::isfinite(points[i][0])||!std::isfinite(points[i][1]))return refuse(10);
        }
        float left=points[0][0],right=left,top=points[0][1],bottom=top;
        for(const auto& point:points){left=point[0]<left?point[0]:left;right=point[0]>right?point[0]:right;
            top=point[1]<top?point[1]:top;bottom=point[1]>bottom?point[1]:bottom;}
        if(left>0||right<1||top>0||bottom<1||left==right||top==bottom)return refuse(11);
        unsigned corners=0;
        for(const auto& point:points){
            if((point[0]!=left&&point[0]!=right)||(point[1]!=top&&point[1]!=bottom))return refuse(11);
            const auto bit=1u<<((point[0]==right?1u:0u)|(point[1]==bottom?2u:0u));
            if(corners&bit)return refuse(11);
            corners|=bit;
        }
        if(corners!=15||points[1][0]==points[2][0]||points[1][1]==points[2][1])return refuse(11);
        std::array<D3D11_VIEWPORT,D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> viewportArray{};
        UINT viewports=UINT(viewportArray.size());context->RSGetViewports(&viewports,viewportArray.data());const auto& viewport=viewportArray[0];
        if(viewports!=1||viewport.TopLeftX||viewport.TopLeftY||viewport.Width!=float(width)||viewport.Height!=float(height)||
            viewport.MinDepth!=0||viewport.MaxDepth!=1)return refuse(12);
        Reference<ID3D11RasterizerState> raster;context->RSGetState(raster.out());
        D3D11_RASTERIZER_DESC desc{};desc.FillMode=D3D11_FILL_SOLID;desc.CullMode=D3D11_CULL_BACK;
        if(raster.value)raster->GetDesc(&desc);
        if(desc.FillMode!=D3D11_FILL_SOLID)return refuse(13);
        if(raster.value){Reference<ID3D11RasterizerState2> latest;
            if(SUCCEEDED(raster->QueryInterface(__uuidof(ID3D11RasterizerState2),reinterpret_cast<void**>(latest.out())))){
                D3D11_RASTERIZER_DESC2 desc2{};latest->GetDesc2(&desc2);
                if(desc2.ForcedSampleCount||desc2.ConservativeRaster!=D3D11_CONSERVATIVE_RASTERIZATION_MODE_OFF)return refuse(13);
            }
            Reference<ID3D11RasterizerState1> extended;
            if(SUCCEEDED(raster->QueryInterface(__uuidof(ID3D11RasterizerState1),reinterpret_cast<void**>(extended.out())))){
                D3D11_RASTERIZER_DESC1 desc1{};extended->GetDesc1(&desc1);if(desc1.ForcedSampleCount)return refuse(13);
            }
        }
        const float area=(points[1][0]-points[0][0])*(points[2][1]-points[0][1])-
            (points[1][1]-points[0][1])*(points[2][0]-points[0][0]);
        const bool front=desc.FrontCounterClockwise?area>0:area<0;
        if((desc.CullMode==D3D11_CULL_BACK&&!front)||(desc.CullMode==D3D11_CULL_FRONT&&front))return refuse(14);
        if(desc.ScissorEnable){UINT rectangles=1;D3D11_RECT rect{};context->RSGetScissorRects(&rectangles,&rect);
            if(rectangles!=1||rect.left>0||rect.top>0||rect.right<LONG(width)||rect.bottom<LONG(height))return refuse(15);}
        Reference<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask=0;context->OMGetBlendState(blend.out(),factors,&mask);
        if((mask&((1u<<samples)-1))!=((1u<<samples)-1))return refuse(16);
        return true;
    }
};
} // namespace stream_bridge::sender::projected_alpha
