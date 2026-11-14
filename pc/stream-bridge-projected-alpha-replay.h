#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11_1.h>
#include <array>
#include <cstddef>

namespace stream_bridge::sender::projected_alpha {
// Our shader copies the CURRENT logical b0 prefix on the GPU. All native
// words, including integer/NaN bit patterns, are retained except the reviewed
// output-alpha policy at byte52. Compilation belongs on the cold path.
inline constexpr char Clone64Hlsl[]=R"(
cbuffer Original : register(b0) { uint4 words[4]; };
RWByteAddressBuffer destination : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint4 value=words[id.x];
    if(id.x==3) value.y=0x3f800000;
    destination.Store4(id.x*16,value);
})";
inline constexpr char Clone80Hlsl[]=R"(
cbuffer Original : register(b0) { uint4 words[5]; };
RWByteAddressBuffer destination : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint4 value=words[id.x];
    if(id.x==3) value.y=0x3f800000;
    destination.Store4(id.x*16,value);
})";
struct Bytecode {const void* data=nullptr;std::size_t size=0;};
using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);

template<class T>struct Reference {
    T* value=nullptr;
    Reference()=default;
    Reference(const Reference&)=delete;
    Reference& operator=(const Reference&)=delete;
    Reference(Reference&& other)noexcept:value(other.value){other.value=nullptr;}
    ~Reference(){if(value)value->Release();}
    T** out()noexcept{return &value;}
    T* operator->()const noexcept{return value;}
};

// Only a new private allocation can become a replay destination. There is no
// constructor/import for an application's texture or an alias of native RGB.
// The producer never exposes this image to native pipeline bindings. Its raw
// resource is used only for the companion's ordered transfer/test readback.
class Image {
    friend class Replay;
    Reference<ID3D11Texture2D> texture_;
    Reference<ID3D11RenderTargetView> target_;
public:
    bool prepare(ID3D11Device* device,UINT width,UINT height)noexcept{
        if(!device||texture_.value||!width||!height||width>16384||height>16384||
            std::size_t(width)*height>16*1024*1024)return false;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        return SUCCEEDED(device->CreateTexture2D(&desc,nullptr,texture_.out()))&&
            SUCCEEDED(device->CreateRenderTargetView(texture_.value,nullptr,target_.out()));
    }
    ID3D11Texture2D* resource()const noexcept{return texture_.value;}
    bool clear(ID3D11DeviceContext* context)noexcept{
        if(!context||!target_.value)return false;
        Reference<ID3D11Device> left,right;
        context->GetDevice(left.out());target_->GetDevice(right.out());
        if(left.value!=right.value)return false;
        constexpr FLOAT zero[4]{};context->ClearRenderTargetView(target_.value,zero);return true;
    }
};

// UNWIRED primitive, not a source/frame authorization mechanism. The caller
// must prove the exact reviewed native PS/VS, input ancestry, current output
// version and complete destination coverage. A known PS alone proves none of
// those. No ordinary resource or native constant buffer is ever modified.
//
// Replay into a DISTINCT alpha-only companion image, retaining native geometry,
// reprojection, filters, UVs and resource bindings. The output is not submitted
// to the sender until a separate current image/source/eye witness exists.
class Replay {
    struct Clone {
        Reference<ID3D11ComputeShader> shader;
        Reference<ID3D11Buffer> raw,constant;
        Reference<ID3D11UnorderedAccessView> write;
    };
    std::array<Clone,2> clones_{};
    Reference<ID3D11Device> device_;
    Reference<ID3D11DeviceContext1> context_;
    Reference<ID3DDeviceContextState> scratch_;
    Reference<ID3D11BlendState> alphaOnly_;
    bool attempted_=false,ready_=false;
    static bool sameDevice(ID3D11DeviceChild* child,ID3D11Device* expected)noexcept{
        if(!child)return false;
        Reference<ID3D11Device> device;child->GetDevice(device.out());
        return device.value==expected;
    }
    static bool viewShape(ID3D11RenderTargetView* view,D3D11_TEXTURE2D_DESC& shape,
        ID3D11Resource*& identity)noexcept{
        D3D11_RENDER_TARGET_VIEW_DESC desc{};view->GetDesc(&desc);
        if(desc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||desc.Texture2D.MipSlice)return false;
        view->GetResource(&identity);
        Reference<ID3D11Texture2D> texture;
        if(!identity||FAILED(identity->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return false;
        texture->GetDesc(&shape);
        return shape.MipLevels==1&&shape.ArraySize==1&&shape.SampleDesc.Count==1;
    }
public:
    Replay()=default;
    Replay(const Replay&)=delete;
    Replay& operator=(const Replay&)=delete;
    bool prepare(ID3D11Device* device,Bytecode clone64,Bytecode clone80)noexcept{
        if(attempted_)return ready_;
        attempted_=true;
        if(!device||!clone64.data||!clone64.size||!clone80.data||!clone80.size)return false;
        Reference<ID3D11Device1> extended;
        if(FAILED(device->QueryInterface(__uuidof(ID3D11Device1),reinterpret_cast<void**>(extended.out()))))return false;
        Reference<ID3D11DeviceContext> immediate;device->GetImmediateContext(immediate.out());
        if(!immediate.value||FAILED(immediate->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(context_.out()))))return false;
        const auto level=device->GetFeatureLevel();
        if(level<D3D_FEATURE_LEVEL_11_0)return false;
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        // Public complete-state save/restore, including native CS/UAV bindings:
        // learn.microsoft.com/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate
        if(FAILED(extended->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,
            __uuidof(ID3D11Device),nullptr,scratch_.out())))return false;
        const std::array<Bytecode,2> programs{{clone64,clone80}};
        for(unsigned i=0;i<clones_.size();++i){
            auto& clone=clones_[i];const auto bytes=64+i*16;
            if(FAILED(device->CreateComputeShader(programs[i].data,programs[i].size,nullptr,clone.shader.out())))return false;
            D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=bytes;buffer.Usage=D3D11_USAGE_DEFAULT;
            buffer.BindFlags=D3D11_BIND_UNORDERED_ACCESS;buffer.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if(FAILED(device->CreateBuffer(&buffer,nullptr,clone.raw.out())))return false;
            D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;
            view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=bytes/4;view.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
            if(FAILED(device->CreateUnorderedAccessView(clone.raw.value,&view,clone.write.out())))return false;
            buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.MiscFlags=0;
            if(FAILED(device->CreateBuffer(&buffer,nullptr,clone.constant.out())))return false;
        }
        D3D11_BLEND_DESC blend{};blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALPHA;
        if(FAILED(device->CreateBlendState(&blend,alphaOnly_.out())))return false;
        device->AddRef();device_.value=device;ready_=true;return true;
    }

    bool replay(ID3D11DeviceContext* context,const Image& image,
        ID3D11PixelShader* reviewedBoundProgram,unsigned logicalConstantBytes,
        Draw originalDraw,UINT vertices,UINT firstVertex)noexcept{
        auto* companion=image.target_.value;
        if(!ready_||!context||!companion||!reviewedBoundProgram||!originalDraw||!vertices||
            (logicalConstantBytes!=64&&logicalConstantBytes!=80)||
            !sameDevice(context,device_.value)||!sameDevice(companion,device_.value))return false;
        Reference<ID3D11DeviceContext1> actual;
        if(FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(actual.out())))||
            actual.value!=context_.value||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        Reference<ID3D11PixelShader> pixel;UINT instances=0;
        context->PSGetShader(pixel.out(),nullptr,&instances);
        if(pixel.value!=reviewedBoundProgram||instances)return false;
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
        Reference<ID3D11DepthStencilView> depth;
        context->OMGetRenderTargets(static_cast<UINT>(targets.size()),targets.data(),depth.out());
        struct Targets {
            decltype(targets)& refs;
            ~Targets(){for(auto* value:refs)if(value)value->Release();}
        } releaseTargets{targets};
        if(!targets[0]||depth.value)return false;
        for(unsigned i=1;i<targets.size();++i)if(targets[i])return false;
        // Preserve every native UAV binding. The supported native contract has
        // no UAVs; reject all public output slots before changing OM state.
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> uavs{};
        const UINT uavCount=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?
            D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,uavCount-1,uavs.data());
        bool hasUav=false;for(auto* value:uavs)if(value){hasUav=true;value->Release();}
        if(hasUav)return false;
        std::array<ID3D11Buffer*,D3D11_SO_BUFFER_SLOT_COUNT> outputs{};
        context->SOGetTargets(static_cast<UINT>(outputs.size()),outputs.data());
        bool hasOutput=false;for(auto* value:outputs)if(value){hasOutput=true;value->Release();}
        if(hasOutput)return false;
        D3D11_TEXTURE2D_DESC nativeShape{},maskShape{};
        Reference<ID3D11Resource> nativeResource,maskResource;
        if(!viewShape(targets[0],nativeShape,nativeResource.value)||!viewShape(companion,maskShape,maskResource.value)||
            nativeResource.value==maskResource.value||nativeShape.Width!=maskShape.Width||nativeShape.Height!=maskShape.Height||
            maskShape.Format!=DXGI_FORMAT_R8G8B8A8_UNORM||maskShape.MiscFlags)return false;
        Reference<ID3D11Buffer> original;UINT offset=0,count=0;
        actual->PSGetConstantBuffers1(0,1,original.out(),&offset,&count);
        if(!original.value||count<logicalConstantBytes/16)return false;
        D3D11_BUFFER_DESC buffer{};original->GetDesc(&buffer);
        if(offset>buffer.ByteWidth/16||logicalConstantBytes>buffer.ByteWidth-offset*16)return false;
        auto& clone=clones_[(logicalConstantBytes-64)/16];
        Reference<ID3D11BlendState> blend;FLOAT factors[4]{};UINT samples=0;
        context->OMGetBlendState(blend.out(),factors,&samples);
        if(blend.value){
            D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);
            constexpr UINT rgb=D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
            if(desc.AlphaToCoverageEnable||desc.RenderTarget[0].BlendEnable||
                (desc.RenderTarget[0].RenderTargetWriteMask&rgb)!=rgb)return false;
            Reference<ID3D11BlendState1> extendedBlend;
            if(SUCCEEDED(blend->QueryInterface(__uuidof(ID3D11BlendState1),reinterpret_cast<void**>(extendedBlend.out())))){
                D3D11_BLEND_DESC1 desc1{};extendedBlend->GetDesc1(&desc1);
                if(desc1.RenderTarget[0].LogicOpEnable)return false;
            }
        }
        Reference<ID3DDeviceContextState> saved;
        actual->SwapDeviceContextState(scratch_.value,saved.out());
        if(!saved.value)return false;
        context->CSSetConstantBuffers(0,1,&original.value);
        actual->CSSetConstantBuffers1(0,1,&original.value,&offset,&count);
        context->CSSetShader(clone.shader.value,nullptr,0);
        context->CSSetUnorderedAccessViews(0,1,&clone.write.value,nullptr);
        context->Dispatch(logicalConstantBytes/16,1,1);
        ID3D11UnorderedAccessView* none=nullptr;context->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        context->CopyResource(clone.constant.value,clone.raw.value);
        actual->SwapDeviceContextState(saved.value,nullptr);
        context->PSSetConstantBuffers(0,1,&clone.constant.value);
        context->OMSetRenderTargets(1,&companion,nullptr);
        context->OMSetBlendState(alphaOnly_.value,nullptr,samples);
        originalDraw(context,vertices,firstVertex);
        context->OMSetRenderTargets(1,targets.data(),nullptr);
        context->OMSetBlendState(blend.value,factors,samples);
        context->PSSetConstantBuffers(0,1,&original.value);
        actual->PSSetConstantBuffers1(0,1,&original.value,&offset,&count);
        return true;
    }
};
} // namespace stream_bridge::sender::projected_alpha
