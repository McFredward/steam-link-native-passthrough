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
#include <cstdint>
#include <atomic>
#include <new>

namespace stream_bridge::sender::projected_alpha {
// Our shader copies the CURRENT logical b0 prefix on the GPU. All native
// words, including integer/NaN bit patterns, are retained except the reviewed
// scene output-alpha policy at byte52. The MSAA resolve clone instead disables
// its optional opaque-output branch at byte0 of logical b2. This affects only
// the companion draw; ordinary native pixels and constants remain unchanged.
// Compilation belongs on the cold path.
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
inline constexpr char CloneResolve32Hlsl[]=R"(
cbuffer Original : register(b0) { uint4 words[2]; };
RWByteAddressBuffer destination : register(u0);
[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {
    uint4 value=words[id.x];
    if(id.x==0) value.x=0;
    destination.Store4(id.x*16,value);
})";
struct Bytecode {const void* data=nullptr;std::size_t size=0;};
using Draw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
struct NativeDrawError {
    DWORD value=0;bool captured=false;
    void capture()noexcept{value=GetLastError();captured=true;}
    ~NativeDrawError(){if(captured)SetLastError(value);}
};

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
// The shipping replay never exposes this image to native pipeline bindings.
// The unshipped same-draw control also uses it as a second private RTV; neither
// operation makes the allocation an authenticated image/frame permit.
// A four-sample image stays private and is resolved into a separate shareable
// single-sample image with the reviewed native resolve geometry.
class Image {
    friend class Replay;
    Reference<ID3D11Texture2D> texture_;
    Reference<ID3D11RenderTargetView> target_;
    Reference<ID3D11ShaderResourceView> source_;
public:
    bool prepare(ID3D11Device* device,UINT width,UINT height,bool shared=false,UINT samples=1,UINT quality=0,
        DXGI_FORMAT format=DXGI_FORMAT_R8G8B8A8_UNORM)noexcept{
        if(!device||texture_.value||!width||!height||width>16384||height>16384||
            std::size_t(width)*height>16*1024*1024||(samples!=1&&samples!=4)||(shared&&samples!=1)||
            (format!=DXGI_FORMAT_R8G8B8A8_UNORM&&format!=DXGI_FORMAT_R16G16B16A16_FLOAT)||
            (format!=DXGI_FORMAT_R8G8B8A8_UNORM&&(shared||samples!=1)))return false;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;
        desc.MipLevels=desc.ArraySize=1;desc.SampleDesc.Count=samples;desc.SampleDesc.Quality=quality;
        desc.Format=format;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags=shared?D3D11_RESOURCE_MISC_SHARED:0;
        return SUCCEEDED(device->CreateTexture2D(&desc,nullptr,texture_.out()))&&
            SUCCEEDED(device->CreateRenderTargetView(texture_.value,nullptr,target_.out()))&&
            SUCCEEDED(device->CreateShaderResourceView(texture_.value,nullptr,source_.out()));
    }
    ID3D11Texture2D* resource()const noexcept{return texture_.value;}
    bool clear(ID3D11DeviceContext* context,const FLOAT* color=nullptr)noexcept{
        if(!context||!target_.value)return false;
        Reference<ID3D11Device> left,right;
        context->GetDevice(left.out());target_->GetDevice(right.out());
        if(left.value!=right.value)return false;
        constexpr FLOAT zero[4]{};context->ClearRenderTargetView(target_.value,color?color:zero);return true;
    }
    void reset()noexcept{
        if(source_.value){source_.value->Release();source_.value=nullptr;}
        if(target_.value){target_.value->Release();target_.value=nullptr;}
        if(texture_.value){texture_.value->Release();texture_.value=nullptr;}
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
    std::array<Clone,3> clones_{};
    Reference<ID3D11Device> device_;
    Reference<ID3D11DeviceContext1> context_;
    Reference<ID3DDeviceContextState> scratch_;
    Reference<ID3D11BlendState> alphaOnly_;
    Reference<ID3D11BlendState> fullColor_;
    struct DepthRecord final:IUnknown {
        inline static constexpr GUID Id{0x3125884c,0x5714,0x4399,{0xaf,0x24,0x5b,0x73,0x36,0x95,0xc2,0xaa}};
        inline static std::atomic<unsigned> live{0};
        std::atomic<ULONG> references{1};std::uint64_t owner=0;bool self=false;Reference<ID3D11DepthStencilState> state;
        ~DepthRecord(){live.fetch_sub(1,std::memory_order_relaxed);}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
            if(!out)return E_POINTER;
            *out=nullptr;
            if(iid!=IID_IUnknown&&iid!=Id)return E_NOINTERFACE;
            *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef()override{return references.fetch_add(1,std::memory_order_relaxed)+1;}
        ULONG STDMETHODCALLTYPE Release()override{const auto n=references.fetch_sub(1,std::memory_order_acq_rel)-1;if(!n)delete this;return n;}
    };
    inline static std::atomic<std::uint64_t> nextOwner_{1};
    const std::uint64_t owner_=nextOwner_.fetch_add(1,std::memory_order_relaxed);
    Reference<ID3D11DepthStencilState> defaultDepth_;
    bool attempted_=false,ready_=false;
    static D3D11_DEPTH_STENCIL_DESC readOnly(D3D11_DEPTH_STENCIL_DESC value)noexcept{
        value.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;value.StencilWriteMask=0;
        value.FrontFace.StencilFailOp=value.FrontFace.StencilDepthFailOp=value.FrontFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        value.BackFace.StencilFailOp=value.BackFace.StencilDepthFailOp=value.BackFace.StencilPassOp=D3D11_STENCIL_OP_KEEP;
        return value;
    }
    Reference<ID3D11DepthStencilState> depthClone(ID3D11DepthStencilState* state)noexcept{
        Reference<ID3D11DepthStencilState> result;
        if(!state){if(defaultDepth_.value){defaultDepth_->AddRef();result.value=defaultDepth_.value;}return result;}
        Reference<IUnknown> unknown;UINT bytes=sizeof(IUnknown*);Reference<DepthRecord> record;
        if(SUCCEEDED(state->GetPrivateData(DepthRecord::Id,&bytes,unknown.out()))&&bytes==sizeof(IUnknown*)&&unknown.value&&
            SUCCEEDED(unknown->QueryInterface(DepthRecord::Id,reinterpret_cast<void**>(record.out())))&&record->owner==owner_&&(record->self||record->state.value)){
            result.value=record->self?state:record->state.value;result->AddRef();}
        return result;
    }
    static bool sameDevice(ID3D11DeviceChild* child,ID3D11Device* expected)noexcept{
        if(!child)return false;
        Reference<ID3D11Device> device;child->GetDevice(device.out());
        return device.value==expected;
    }
    static bool viewShape(ID3D11RenderTargetView* view,D3D11_TEXTURE2D_DESC& shape,
        ID3D11Resource*& identity)noexcept{
        D3D11_RENDER_TARGET_VIEW_DESC desc{};view->GetDesc(&desc);
        if((desc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D||desc.Texture2D.MipSlice)&&
            desc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS)return false;
        view->GetResource(&identity);
        Reference<ID3D11Texture2D> texture;
        if(!identity||FAILED(identity->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return false;
        texture->GetDesc(&shape);
        return shape.MipLevels==1&&shape.ArraySize==1&&(shape.SampleDesc.Count==1||shape.SampleDesc.Count==4);
    }
public:
    Replay()=default;
    Replay(const Replay&)=delete;
    Replay& operator=(const Replay&)=delete;
    bool prepare(ID3D11Device* device,Bytecode clone64,Bytecode clone80,Bytecode clone32={})noexcept{
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
        const std::array<Bytecode,3> programs{{clone64,clone80,clone32}};
        for(unsigned i=0;i<clones_.size();++i){
            if(i==2&&(!clone32.data||!clone32.size))continue;
            auto& clone=clones_[i];const auto bytes=i==2?32:64+i*16;
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
        blend.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        if(FAILED(device->CreateBlendState(&blend,fullColor_.out())))return false;
        D3D11_DEPTH_STENCIL_DESC defaultDepth{};defaultDepth.DepthEnable=TRUE;defaultDepth.DepthFunc=D3D11_COMPARISON_LESS;
        defaultDepth.StencilReadMask=D3D11_DEFAULT_STENCIL_READ_MASK;
        defaultDepth.FrontFace.StencilFunc=defaultDepth.BackFace.StencilFunc=D3D11_COMPARISON_ALWAYS;
        const auto immutableDepth=readOnly(defaultDepth);
        if(FAILED(device->CreateDepthStencilState(&immutableDepth,defaultDepth_.out())))return false;
        device->AddRef();device_.value=device;ready_=true;return true;
    }

    // Cold callback only: retain the original comparison/read mask, suppress
    // every depth/stencil write in the companion draw. The ordinary draw still
    // uses its original state/view once. Records die with native state objects.
    void observeDepth(ID3D11DepthStencilState* state)noexcept{
        if(!ready_||!owner_||!state||!sameDevice(state,device_.value)||depthClone(state).value)return;
        if(DepthRecord::live.fetch_add(1,std::memory_order_acq_rel)>=256){DepthRecord::live.fetch_sub(1,std::memory_order_release);return;}
        Reference<DepthRecord> record;record.value=new(std::nothrow) DepthRecord;
        if(!record.value){DepthRecord::live.fetch_sub(1,std::memory_order_release);return;}
        record->owner=owner_;D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);desc=readOnly(desc);
        if(SUCCEEDED(device_->CreateDepthStencilState(&desc,record->state.out()))){
            // D3D11 may intern an already read-only state. Do not create an
            // object -> private record -> same object reference cycle.
            if(record->state.value==state){record->self=true;record->state->Release();record->state.value=nullptr;}
            (void)state->SetPrivateDataInterface(DepthRecord::Id,record.value);
        }
    }
    static bool conditionalDepth(ID3D11DeviceContext* context)noexcept{
        Reference<ID3D11DepthStencilView> view;context->OMGetRenderTargets(0,nullptr,view.out());if(!view.value)return false;
        Reference<ID3D11DepthStencilState> state;context->OMGetDepthStencilState(state.out(),nullptr);
        if(!state.value)return true;
        D3D11_DEPTH_STENCIL_DESC desc{};state->GetDesc(&desc);
        return (desc.DepthEnable&&desc.DepthFunc!=D3D11_COMPARISON_ALWAYS)||
            (desc.StencilEnable&&(desc.FrontFace.StencilFunc!=D3D11_COMPARISON_ALWAYS||desc.BackFace.StencilFunc!=D3D11_COMPARISON_ALWAYS));
    }
    // Unshipped scene-policy experiment: run the ORIGINAL draw exactly once with
    // its existing alpha-policy constant enabled. No scene texture is sampled
    // a second time and no RGB arithmetic, shader, RTV, depth/stencil state or
    // geometry is replaced. The caller must authenticate source ancestry and
    // retain exact native output/Present synchronization before exporting A.
    bool nativeAlpha(ID3D11DeviceContext* context,ID3D11PixelShader* program,
        unsigned logicalBytes,bool resolving,Draw original,UINT vertices,UINT start,DWORD incoming=GetLastError())noexcept{
        NativeDrawError error;
        if(!ready_||!context||!program||!original||!vertices||
            !sameDevice(context,device_.value)||context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE||
            resolving||(logicalBytes!=0&&logicalBytes!=64&&logicalBytes!=80))return false;
        Reference<ID3D11DeviceContext1> actual;
        if(FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(actual.out())))||
            actual.value!=context_.value)return false;
        Reference<ID3D11PixelShader> bound;UINT classes=0;context->PSGetShader(bound.out(),nullptr,&classes);
        if(bound.value!=program||classes)return false;
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
        context->OMGetRenderTargets(UINT(targets.size()),targets.data(),nullptr);
        struct TargetRefs {decltype(targets)& values;~TargetRefs(){for(auto* v:values)if(v)v->Release();}} refs{targets};
        if(!targets[0])return false;
        for(unsigned i=1;i<targets.size();++i)if(targets[i])return false;
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> uavs{};
        const UINT slots=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,slots-1,uavs.data());
        bool writable=false;for(auto* v:uavs)if(v){writable=true;v->Release();}if(writable)return false;
        Reference<ID3D11BlendState> blend;context->OMGetBlendState(blend.out(),nullptr,nullptr);
        if(blend.value){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);
            if(desc.AlphaToCoverageEnable||desc.RenderTarget[0].BlendEnable||
                desc.RenderTarget[0].RenderTargetWriteMask!=D3D11_COLOR_WRITE_ENABLE_ALL)return false;
            Reference<ID3D11BlendState1> extended;
            if(SUCCEEDED(blend->QueryInterface(__uuidof(ID3D11BlendState1),reinterpret_cast<void**>(extended.out())))){
                D3D11_BLEND_DESC1 current{};extended->GetDesc1(&current);if(current.RenderTarget[0].LogicOpEnable)return false;
            }
        }
        if(!logicalBytes){SetLastError(incoming);original(context,vertices,start);error.capture();return true;}
        const UINT slot=resolving?2:0;
        Reference<ID3D11Buffer> native;UINT offset=0,count=0;
        actual->PSGetConstantBuffers1(slot,1,native.out(),&offset,&count);
        if(!native.value||count<logicalBytes/16)return false;
        D3D11_BUFFER_DESC desc{};native->GetDesc(&desc);
        if(offset>desc.ByteWidth/16||logicalBytes>desc.ByteWidth-offset*16)return false;
        auto& clone=clones_[resolving?2:(logicalBytes-64)/16];if(!clone.shader.value)return false;
        Reference<ID3DDeviceContextState> saved;
        actual->SwapDeviceContextState(scratch_.value,saved.out());if(!saved.value)return false;
        context->CSSetConstantBuffers(0,1,&native.value);actual->CSSetConstantBuffers1(0,1,&native.value,&offset,&count);
        context->CSSetShader(clone.shader.value,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&clone.write.value,nullptr);
        context->Dispatch(logicalBytes/16,1,1);
        ID3D11UnorderedAccessView* none=nullptr;context->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        context->CopyResource(clone.constant.value,clone.raw.value);actual->SwapDeviceContextState(saved.value,nullptr);
        context->PSSetConstantBuffers(slot,1,&clone.constant.value);
        SetLastError(incoming);original(context,vertices,start);error.capture();
        context->PSSetConstantBuffers(slot,1,&native.value);actual->PSSetConstantBuffers1(slot,1,&native.value,&offset,&count);
        return true;
    }
    // Unshipped MRT primitive. A cold-prepared, separately reviewed augmented
    // program must leave its native SV_Target0 unchanged and emit real coverage
    // to SV_Target1. It is NOT obtained by flipping the native resolve policy:
    // that policy can change RGB as well as A. No root/image permit is granted
    // here; the production ancestry/Present controller is not wired to this.
    bool nativeCapture(ID3D11DeviceContext* context,const Image& capture,
        ID3D11PixelShader* nativeProgram,ID3D11PixelShader* augmentedProgram,
        Draw original,UINT vertices,UINT start,const Image* alphaInput=nullptr,DWORD incoming=GetLastError())noexcept{
        NativeDrawError error;
        if(!ready_||!context||!nativeProgram||!augmentedProgram||!original||!vertices||
            !capture.target_.value||!sameDevice(context,device_.value)||
            !sameDevice(augmentedProgram,device_.value)||!sameDevice(capture.target_.value,device_.value)||
            context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        Reference<ID3D11DeviceContext1> actual;
        if(FAILED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(actual.out())))||
            actual.value!=context_.value)return false;
        Reference<ID3D11PixelShader> bound;UINT classes=0;context->PSGetShader(bound.out(),nullptr,&classes);
        if(bound.value!=nativeProgram||classes)return false;
        std::array<ID3D11RenderTargetView*,D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets{};
        Reference<ID3D11DepthStencilView> depth;context->OMGetRenderTargets(UINT(targets.size()),targets.data(),depth.out());
        struct TargetRefs {decltype(targets)& values;~TargetRefs(){for(auto* v:values)if(v)v->Release();}} refs{targets};
        if(!targets[0])return false;
        for(unsigned i=1;i<targets.size();++i)if(targets[i])return false;
        Reference<ID3D11Resource> output;Reference<ID3D11Texture2D> texture;targets[0]->GetResource(output.out());
        if(FAILED(output->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||
            texture.value==capture.texture_.value)return false;
        D3D11_TEXTURE2D_DESC nativeShape{},privateShape{};texture->GetDesc(&nativeShape);capture.texture_->GetDesc(&privateShape);
        D3D11_RENDER_TARGET_VIEW_DESC nativeView{};targets[0]->GetDesc(&nativeView);
        if(nativeShape.Width!=privateShape.Width||nativeShape.Height!=privateShape.Height||
            nativeShape.SampleDesc.Count!=privateShape.SampleDesc.Count||nativeShape.SampleDesc.Quality!=privateShape.SampleDesc.Quality||
            nativeShape.MipLevels!=1||nativeShape.ArraySize!=1||
            (nativeView.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D&&nativeView.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS))return false;
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> uavs{};
        const UINT slots=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,slots-1,uavs.data());
        bool writable=false;for(auto* v:uavs)if(v){writable=true;v->Release();}if(writable)return false;
        Reference<ID3D11BlendState> blend;std::array<FLOAT,4> factor{};UINT sampleMask=0;
        context->OMGetBlendState(blend.out(),factor.data(),&sampleMask);
        if(blend.value){D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);
            if(desc.AlphaToCoverageEnable||desc.RenderTarget[0].BlendEnable||
                desc.RenderTarget[0].RenderTargetWriteMask!=D3D11_COLOR_WRITE_ENABLE_ALL)return false;
            Reference<ID3D11BlendState1> extended;
            if(SUCCEEDED(blend->QueryInterface(__uuidof(ID3D11BlendState1),reinterpret_cast<void**>(extended.out())))){
                D3D11_BLEND_DESC1 current{};extended->GetDesc1(&current);if(current.RenderTarget[0].LogicOpEnable)return false;
            }
        }
        Reference<ID3D11ShaderResourceView> priorInput;
        if(alphaInput){
            if(!alphaInput->source_.value||!sameDevice(alphaInput->source_.value,device_.value)||
                alphaInput->texture_.value==texture.value||alphaInput->texture_.value==capture.texture_.value)return false;
            // The augmented resolve reads its private MSAA alpha at t1. It
            // leaves native t0, b2 and every original instruction unchanged.
            context->PSGetShaderResources(1,1,priorInput.out());
            context->PSSetShaderResources(1,1,&alphaInput->source_.value);
        }
        ID3D11RenderTargetView* pair[]{targets[0],capture.target_.value};
        context->OMSetRenderTargets(2,pair,depth.value);
        // RT0 was already unblended/full-write; default gives the same native
        // colour behaviour and enables the new RT1. Preserve sample coverage.
        context->OMSetBlendState(nullptr,nullptr,sampleMask);
        context->PSSetShader(augmentedProgram,nullptr,0);SetLastError(incoming);original(context,vertices,start);error.capture();
        context->PSSetShader(nativeProgram,nullptr,0);
        context->OMSetRenderTargets(1,targets.data(),depth.value);
        context->OMSetBlendState(blend.value,factor.data(),sampleMask);
        if(alphaInput)context->PSSetShaderResources(1,1,&priorInput.value);
        return true;
    }
    bool replay(ID3D11DeviceContext* context,const Image& image,
        ID3D11PixelShader* reviewedBoundProgram,unsigned logicalConstantBytes,
        Draw originalDraw,UINT vertices,UINT firstVertex)noexcept{
        if(logicalConstantBytes!=64&&logicalConstantBytes!=80)return false;
        return draw(context,image,reviewedBoundProgram,logicalConstantBytes,originalDraw,vertices,firstVertex,nullptr,false,false);
    }
    // The X program retains RGBA. Its private FP16 shadow also retains RGB,
    // keeping later native colour-dependent filtering intact. It never leaves
    // the producer, and only its alpha reaches the shareable output mask.
    bool intermediate(ID3D11DeviceContext* context,const Image& image,ID3D11PixelShader* program,
        Draw originalDraw,UINT vertices,UINT start)noexcept{
        return draw(context,image,program,0,originalDraw,vertices,start,nullptr,false,true);
    }
    bool filtered(ID3D11DeviceContext* context,const Image& image,const Image& source,
        ID3D11PixelShader* program,unsigned bytes,Draw originalDraw,UINT vertices,UINT start)noexcept{
        if(bytes!=64&&bytes!=80)return false;
        return draw(context,image,program,bytes,originalDraw,vertices,start,&source,false,false);
    }
    bool resolve(ID3D11DeviceContext* context,const Image& image,const Image& source,
        ID3D11PixelShader* reviewedBoundProgram,Draw originalDraw,UINT vertices,UINT firstVertex)noexcept{
        if(!clones_[2].shader.value)return false;
        return draw(context,image,reviewedBoundProgram,32,originalDraw,vertices,firstVertex,&source,true,false);
    }
private:
    bool draw(ID3D11DeviceContext* context,const Image& image,
        ID3D11PixelShader* reviewedBoundProgram,unsigned logicalConstantBytes,
        Draw originalDraw,UINT vertices,UINT firstVertex,const Image* replacement,bool resolving,bool intermediatePass)noexcept{
        auto* companion=image.target_.value;
        if(!ready_||!context||!companion||!reviewedBoundProgram||!originalDraw||!vertices||
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
        if(!targets[0])return false;
        Reference<ID3D11DepthStencilState> originalDepth,companionDepth;UINT stencilRef=0;
        if(depth.value){context->OMGetDepthStencilState(originalDepth.out(),&stencilRef);auto clone=depthClone(originalDepth.value);companionDepth.value=clone.value;clone.value=nullptr;
            if(!companionDepth.value)return false;}
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
            nativeShape.SampleDesc.Count!=maskShape.SampleDesc.Count||nativeShape.SampleDesc.Quality!=maskShape.SampleDesc.Quality||
            maskShape.Format!=(intermediatePass?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM)||
            (maskShape.MiscFlags&&maskShape.MiscFlags!=D3D11_RESOURCE_MISC_SHARED))return false;
        Reference<ID3D11ShaderResourceView> originalSource;
        if(replacement){
            if(!replacement->source_.value||!sameDevice(replacement->source_.value,device_.value))return false;
            D3D11_TEXTURE2D_DESC sourceShape{};replacement->texture_->GetDesc(&sourceShape);
            if(resolving&&(nativeShape.SampleDesc.Count!=1||sourceShape.SampleDesc.Count!=4||
                sourceShape.Width!=maskShape.Width||sourceShape.Height!=maskShape.Height))return false;
            if(!resolving&&(sourceShape.SampleDesc.Count!=1||sourceShape.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))return false;
            context->PSGetShaderResources(0,1,originalSource.out());
            if(!originalSource.value)return false;
            D3D11_SHADER_RESOURCE_VIEW_DESC sourceView{};originalSource->GetDesc(&sourceView);
            Reference<ID3D11Resource> resource;originalSource->GetResource(resource.out());
            Reference<ID3D11Texture2D> texture;
            const auto expected=resolving?D3D11_SRV_DIMENSION_TEXTURE2DMS:D3D11_SRV_DIMENSION_TEXTURE2D;
            if(sourceView.ViewDimension!=expected||!resource.value||
                FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return false;
            D3D11_TEXTURE2D_DESC actualShape{};texture->GetDesc(&actualShape);
            if(actualShape.SampleDesc.Count!=sourceShape.SampleDesc.Count||actualShape.SampleDesc.Quality!=sourceShape.SampleDesc.Quality||
                actualShape.Width!=sourceShape.Width||actualShape.Height!=sourceShape.Height||actualShape.MipLevels!=1||actualShape.ArraySize!=1)return false;
            if(!resolving&&sourceView.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
        }
        const UINT constantSlot=resolving?2:0;
        Reference<ID3D11Buffer> original;UINT offset=0,count=0;
        Clone* clone=nullptr;
        if(!intermediatePass){
            actual->PSGetConstantBuffers1(constantSlot,1,original.out(),&offset,&count);
            if(!original.value||count<logicalConstantBytes/16)return false;
            D3D11_BUFFER_DESC buffer{};original->GetDesc(&buffer);
            if(offset>buffer.ByteWidth/16||logicalConstantBytes>buffer.ByteWidth-offset*16)return false;
            clone=&clones_[resolving?2:(logicalConstantBytes-64)/16];
        }
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
        if(clone){
            Reference<ID3DDeviceContextState> saved;
            actual->SwapDeviceContextState(scratch_.value,saved.out());if(!saved.value)return false;
            context->CSSetConstantBuffers(0,1,&original.value);actual->CSSetConstantBuffers1(0,1,&original.value,&offset,&count);
            context->CSSetShader(clone->shader.value,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&clone->write.value,nullptr);
            context->Dispatch(logicalConstantBytes/16,1,1);
            ID3D11UnorderedAccessView* none=nullptr;context->CSSetUnorderedAccessViews(0,1,&none,nullptr);
            context->CopyResource(clone->constant.value,clone->raw.value);actual->SwapDeviceContextState(saved.value,nullptr);
            context->PSSetConstantBuffers(constantSlot,1,&clone->constant.value);
        }
        if(replacement)context->PSSetShaderResources(0,1,&replacement->source_.value);
        context->OMSetRenderTargets(1,&companion,depth.value);
        if(depth.value)context->OMSetDepthStencilState(companionDepth.value,stencilRef);
        context->OMSetBlendState(intermediatePass?fullColor_.value:alphaOnly_.value,nullptr,samples);
        originalDraw(context,vertices,firstVertex);
        context->OMSetRenderTargets(1,targets.data(),depth.value);
        if(depth.value)context->OMSetDepthStencilState(originalDepth.value,stencilRef);
        context->OMSetBlendState(blend.value,factors,samples);
        if(clone){context->PSSetConstantBuffers(constantSlot,1,&original.value);
            actual->PSSetConstantBuffers1(constantSlot,1,&original.value,&offset,&count);}
        if(replacement)context->PSSetShaderResources(0,1,&originalSource.value);
        return true;
    }
};
} // namespace stream_bridge::sender::projected_alpha
