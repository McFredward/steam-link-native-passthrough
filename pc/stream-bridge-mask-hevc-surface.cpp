#include "stream-bridge-mask-hevc-surface.h"
#include <d3dcompiler.h>
#include <cstring>

namespace stream_bridge::sender {
namespace {
template<class T>void release(T*& value){if(value){value->Release();value=nullptr;}}
constexpr char Shader[]=R"(
Texture2D<float> transmittance : register(t0);
float4 vs(uint id : SV_VertexID) : SV_Position {
    float2 p=float2((id<<1)&2,id&2);
    return float4(p*float2(2,-2)+float2(-1,1),0,1);
}
float4 ps(float4 p : SV_Position) : SV_Target {
    return 1.0-transmittance.Load(int3(int2(p.xy),0));
}
)";
struct Saved {
    ID3D11DeviceContext* c;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11GeometryShader* gs=nullptr;ID3D11HullShader* hs=nullptr;ID3D11DomainShader* ds=nullptr;
    ID3D11InputLayout* layout=nullptr;D3D11_PRIMITIVE_TOPOLOGY topology{};
    ID3D11RasterizerState* raster=nullptr;ID3D11BlendState* blend=nullptr;
    ID3D11DepthStencilState* depth=nullptr;UINT stencil=0,mask=0;
    FLOAT factor[4]{};std::array<ID3D11RenderTargetView*,8> targets{};
    ID3D11DepthStencilView* depthView=nullptr;ID3D11ShaderResourceView* resource=nullptr;
    std::array<D3D11_VIEWPORT,16> viewports{};UINT count=16;
    ID3D11Predicate* predicate=nullptr;BOOL predication=FALSE;
    bool compatible=true,changed=false;
    explicit Saved(ID3D11DeviceContext* context,UINT uavCount):c(context){
        // Dynamic shader linkage and OM UAVs are not part of this conversion
        // contract. Refuse without changing native state rather than losing them.
        UINT n=0;c->VSGetShader(&vs,nullptr,&n);compatible=compatible&&n==0;
        n=0;c->PSGetShader(&ps,nullptr,&n);compatible=compatible&&n==0;
        n=0;c->GSGetShader(&gs,nullptr,&n);compatible=compatible&&n==0;
        n=0;c->HSGetShader(&hs,nullptr,&n);compatible=compatible&&n==0;
        n=0;c->DSGetShader(&ds,nullptr,&n);compatible=compatible&&n==0;
        std::array<ID3D11UnorderedAccessView*,64> uavs{};
        c->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,uavCount,uavs.data());
        for(UINT i=0;i<uavCount;++i){compatible=compatible&&uavs[i]==nullptr;release(uavs[i]);}
        c->IAGetInputLayout(&layout);c->IAGetPrimitiveTopology(&topology);
        c->RSGetState(&raster);c->RSGetViewports(&count,viewports.data());
        c->OMGetBlendState(&blend,factor,&mask);c->OMGetDepthStencilState(&depth,&stencil);
        c->OMGetRenderTargets(static_cast<UINT>(targets.size()),targets.data(),&depthView);
        c->PSGetShaderResources(0,1,&resource);c->GetPredication(&predicate,&predication);
    }
    ~Saved(){
        if(changed){
            ID3D11ShaderResourceView* empty=nullptr;c->PSSetShaderResources(0,1,&empty);
            c->OMSetRenderTargets(static_cast<UINT>(targets.size()),targets.data(),depthView);
            c->VSSetShader(vs,nullptr,0);c->PSSetShader(ps,nullptr,0);
            c->GSSetShader(gs,nullptr,0);c->HSSetShader(hs,nullptr,0);c->DSSetShader(ds,nullptr,0);
            c->IASetInputLayout(layout);c->IASetPrimitiveTopology(topology);
            c->RSSetState(raster);c->RSSetViewports(count,viewports.data());
            c->OMSetBlendState(blend,factor,mask);c->OMSetDepthStencilState(depth,stencil);
            c->PSSetShaderResources(0,1,&resource);c->SetPredication(predicate,predication);
        }
        release(vs);release(ps);release(gs);release(hs);release(ds);release(layout);
        release(raster);release(blend);release(depth);release(depthView);release(resource);release(predicate);
        for(auto*& target:targets)release(target);
    }
};
}
struct MaskHevcSurfaces::Impl {
    ID3D11Device* device=nullptr;ID3D11ShaderResourceView* source=nullptr;
    ID3D11VertexShader* vs=nullptr;ID3D11PixelShader* ps=nullptr;
    ID3D11RasterizerState* raster=nullptr;ID3D11DepthStencilState* depth=nullptr;
    std::array<ID3D11Texture2D*,4> textures{};
    std::array<ID3D11RenderTargetView*,4> luma{},chroma{};
    UINT width=0,height=0;
    void reset(){
        for(auto*& p:luma)release(p);
        for(auto*& p:chroma)release(p);
        for(auto*& p:textures)release(p);
        release(source);release(vs);release(ps);release(raster);release(depth);release(device);width=height=0;
    }
};
MaskHevcSurfaces::MaskHevcSurfaces():impl_(new Impl){}
MaskHevcSurfaces::~MaskHevcSurfaces(){resetCold();}
void MaskHevcSurfaces::resetCold()noexcept{impl_->reset();}
std::array<ID3D11Texture2D*,4> MaskHevcSurfaces::textures()const noexcept{return impl_->textures;}
bool MaskHevcSurfaces::prepareCold(ID3D11Device* device,ID3D11Texture2D* input)noexcept{
    resetCold();if(!device||!input)return false;auto& s=*impl_;
    D3D11_TEXTURE2D_DESC desc{};input->GetDesc(&desc);
    ID3D11Device* actual=nullptr;input->GetDevice(&actual);const bool same=actual==device;release(actual);
    if(!same||desc.Format!=DXGI_FORMAT_R8_UNORM||desc.SampleDesc.Count!=1||desc.MipLevels!=1||desc.ArraySize!=1||
       !desc.Width||!desc.Height||(desc.Width&1)||(desc.Height&1)||desc.Width>8192||desc.Height>8192||
       !(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE))return false;
    const auto fail=[&](){s.reset();return false;};
    device->AddRef();s.device=device;s.width=desc.Width;s.height=desc.Height;
    if(FAILED(device->CreateShaderResourceView(input,nullptr,&s.source)))return fail();
    ID3DBlob* vertex=nullptr;ID3DBlob* pixel=nullptr;
    const auto compiled=[&](const char* entry,const char* profile,ID3DBlob** blob){
        ID3DBlob* errors=nullptr;const auto result=D3DCompile(Shader,sizeof(Shader)-1,"sfnp-owned-mask-conversion",
            nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,blob,&errors);
        release(errors);return SUCCEEDED(result);
    };
    const bool shaders=compiled("vs","vs_4_0",&vertex)&&compiled("ps","ps_4_0",&pixel)&&
        SUCCEEDED(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&s.vs))&&
        SUCCEEDED(device->CreatePixelShader(pixel->GetBufferPointer(),pixel->GetBufferSize(),nullptr,&s.ps));
    release(vertex);release(pixel);if(!shaders)return fail();
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
    D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;
    if(FAILED(device->CreateRasterizerState(&raster,&s.raster))||FAILED(device->CreateDepthStencilState(&depth,&s.depth)))return fail();
    desc.Format=DXGI_FORMAT_NV12;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=0;
    desc.BindFlags=D3D11_BIND_RENDER_TARGET;desc.MiscFlags=0;
    for(unsigned i=0;i<s.textures.size();++i){
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;view.Format=DXGI_FORMAT_R8_UNORM;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&s.textures[i]))||
           FAILED(device->CreateRenderTargetView(s.textures[i],&view,&s.luma[i])))return fail();
        view.Format=DXGI_FORMAT_R8G8_UNORM;
        if(FAILED(device->CreateRenderTargetView(s.textures[i],&view,&s.chroma[i])))return fail();
    }
    return true;
}
bool MaskHevcSurfaces::write(ID3D11DeviceContext* context,unsigned slot)noexcept{
    auto& s=*impl_;if(!context||slot>=s.textures.size()||!s.source||!s.textures[slot])return false;
    ID3D11Device* device=nullptr;context->GetDevice(&device);const bool same=device==s.device;
    const UINT uavCount=device&&device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?64:8;
    release(device);if(!same)return false;
    Saved saved(context,uavCount);if(!saved.compatible)return false;saved.changed=true;
    const FLOAT neutral[4]{128.f/255.f,128.f/255.f,128.f/255.f,128.f/255.f};
    context->ClearRenderTargetView(s.chroma[slot],neutral);
    context->OMSetRenderTargets(1,&s.luma[slot],nullptr);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);context->OMSetDepthStencilState(s.depth,0);
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(s.vs,nullptr,0);context->PSSetShader(s.ps,nullptr,0);
    context->GSSetShader(nullptr,nullptr,0);context->HSSetShader(nullptr,nullptr,0);context->DSSetShader(nullptr,nullptr,0);
    context->RSSetState(s.raster);const D3D11_VIEWPORT viewport{0,0,FLOAT(s.width),FLOAT(s.height),0,1};context->RSSetViewports(1,&viewport);
    context->SetPredication(nullptr,FALSE);context->PSSetShaderResources(0,1,&s.source);context->Draw(3,0);
    return true;
}
}
