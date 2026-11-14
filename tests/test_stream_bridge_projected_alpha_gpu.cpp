#include "pc/stream-bridge-projected-alpha-replay.h"
#include <d3dcompiler.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace stream_bridge::sender::projected_alpha;
namespace {
unsigned checks=0,draws=0;
ID3D11Buffer* replayedConstants=nullptr;
UINT replayVertices=0,replayStart=0;
void check(bool pass,const char* label){++checks;if(!pass)throw std::runtime_error(label);}
template<class T>using Com=Reference<T>;
void STDMETHODCALLTYPE countedDraw(ID3D11DeviceContext* context,UINT vertices,UINT start){
    ++draws;context->Draw(vertices,start);
    if(replayedConstants)replayedConstants->Release();
    context->PSGetConstantBuffers(0,1,&replayedConstants);replayVertices=vertices;replayStart=start;
}
// OWNED fixture, deliberately not native shader text. RGB and filtered A use
// the same input/UVs but the output policy can overwrite A with1. The fifth
// vector independently affects UV/RGB, exercising both reviewed CB sizes.
constexpr char Fixture[]=R"(
cbuffer Native : register(b0) { float4 parameters[5]; };
Texture2D<float4> source : register(t0);
float4 vertex(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 pixel(float4 pos:SV_Position):SV_Target {
    uint2 xy=uint2(pos.xy)+uint2(parameters[0].xy);
    float4 value=source.Load(int3(xy,0));
    value.rgb=value.rgb*parameters[1].xyz+parameters[4].xyz;
    value.a=parameters[3].y==0?1:value.a;
    return value;
}
float4 pixel64(float4 pos:SV_Position):SV_Target {
    uint2 xy=uint2(pos.xy)+uint2(parameters[0].xy);
    float4 value=source.Load(int3(xy,0));
    value.rgb=value.rgb*parameters[1].xyz;
    value.a=parameters[3].y==0?1:value.a;
    return value;
}
RWByteAddressBuffer scratch : register(u0);
[numthreads(1,1,1)] void compute(uint3 id:SV_DispatchThreadID){scratch.Store(0,71+id.x);}
)";
Com<ID3DBlob> compile(const char* source,const char* entry,const char* profile){
    Com<ID3DBlob> code,error;
    const auto hr=D3DCompile(source,std::strlen(source),"sfnp-owned-alpha-replay-fixture",nullptr,nullptr,
        entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,code.out(),error.out());
    if(FAILED(hr)&&error.value)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));
    check(SUCCEEDED(hr),"owned HLSL compilation");
    return code;
}
struct Texture {
    Com<ID3D11Texture2D> image,read;
    Com<ID3D11RenderTargetView> target;
    Com<ID3D11ShaderResourceView> source;
    bool prepare(ID3D11Device* device,UINT width,UINT height){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,image.out()))||FAILED(device->CreateRenderTargetView(image.value,nullptr,target.out()))||
            FAILED(device->CreateShaderResourceView(image.value,nullptr,source.out())))return false;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device->CreateTexture2D(&desc,nullptr,read.out()));
    }
    std::vector<std::uint8_t> bytes(ID3D11DeviceContext* context){
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);context->CopyResource(read.value,image.value);
        D3D11_MAPPED_SUBRESOURCE map{};
        check(SUCCEEDED(context->Map(read.value,0,D3D11_MAP_READ,0,&map)),"test-only owned image readback");
        std::vector<std::uint8_t> result(desc.Width*desc.Height*4);
        for(UINT y=0;y<desc.Height;++y)std::memcpy(result.data()+y*desc.Width*4,
            static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch,desc.Width*4);
        context->Unmap(read.value,0);return result;
    }
};
void run(UINT creationFlags){
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,creationFlags,requested,2,
        D3D11_SDK_VERSION,device.out(),&level,context.out())),"actual Windows WARP device");
    Com<ID3D11DeviceContext1> extended;
    check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"actual immediate context1");
    auto clone64=compile(Clone64Hlsl,"main","cs_5_0"),clone80=compile(Clone80Hlsl,"main","cs_5_0");
    Replay replay;
    check(replay.prepare(device.value,{clone64->GetBufferPointer(),clone64->GetBufferSize()},
        {clone80->GetBufferPointer(),clone80->GetBufferSize()}),"cold bounded clone/state/blend preparation");
    auto vsCode=compile(Fixture,"vertex","vs_5_0"),ps64Code=compile(Fixture,"pixel64","ps_5_0"),
        ps80Code=compile(Fixture,"pixel","ps_5_0"),csCode=compile(Fixture,"compute","cs_5_0");
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps64,ps80;Com<ID3D11ComputeShader> cs;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(ps64Code->GetBufferPointer(),ps64Code->GetBufferSize(),nullptr,ps64.out()))&&
        SUCCEEDED(device->CreatePixelShader(ps80Code->GetBufferPointer(),ps80Code->GetBufferSize(),nullptr,ps80.out()))&&
        SUCCEEDED(device->CreateComputeShader(csCode->GetBufferPointer(),csCode->GetBufferSize(),nullptr,cs.out())),"owned fixture programs");
    Texture source,native;
    Image mask,wrongSize;
    constexpr UINT Width=37,Height=29;
    check(source.prepare(device.value,Width,Height)&&native.prepare(device.value,Width,Height)&&
        mask.prepare(device.value,Width,Height)&&wrongSize.prepare(device.value,Width+1,Height),"owned non-square coverage and RGB images");
    std::vector<std::array<std::uint8_t,4>> pixels(Width*Height);
    for(unsigned i=0;i<pixels.size();++i)pixels[i]={{std::uint8_t(7+i%213),91,173,std::uint8_t((i%3)==0?0:((i%3)==1?128:255))}};
    context->UpdateSubresource(source.image.value,0,nullptr,pixels.data(),Width*4,0);
    D3D11_BLEND_DESC blendDesc{};blendDesc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    Com<ID3D11BlendState> blend;check(SUCCEEDED(device->CreateBlendState(&blendDesc,blend.out())),"native blend state");
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;
    rasterDesc.ScissorEnable=TRUE;rasterDesc.DepthClipEnable=TRUE;
    Com<ID3D11RasterizerState> raster;check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,raster.out())),"native rasterizer/scissor");
    D3D11_BUFFER_DESC rawDesc{};rawDesc.ByteWidth=64;rawDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    rawDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    Com<ID3D11Buffer> scratch;Com<ID3D11UnorderedAccessView> write;
    check(SUCCEEDED(device->CreateBuffer(&rawDesc,nullptr,scratch.out())),"native compute scratch");
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements=16;uav.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    check(SUCCEEDED(device->CreateUnorderedAccessView(scratch.value,&uav,write.out())),"native UAV");
    for(unsigned logicalBytes:{64u,80u})for(unsigned offsetBytes:{0u,256u})for(UINT sampleMask:{0x12345679u,0x12345678u}){
        const unsigned allocation=offsetBytes?512:logicalBytes;
        std::vector<std::uint32_t> words(allocation/4,0x7fc12345u); // outside logical CB must survive
        std::array<float,20> values{};values[4]=.5f;values[5]=.75f;values[6]=.875f;
        values[13]=0;values[16]=.03125f;values[17]=.0625f;values[18]=.125f;
        std::memcpy(words.data()+offsetBytes/4,values.data(),logicalBytes);
        words[offsetBytes/4+8]=0x7fc12345u;words[offsetBytes/4+9]=0x80000000u;
        for(auto& pixel:pixels)pixel[3]=std::uint8_t(255-pixel[3]);
        context->UpdateSubresource(source.image.value,0,nullptr,pixels.data(),Width*4,0);
        D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=allocation;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{words.data(),0,0};Com<ID3D11Buffer> cb,cbRead;
        check(SUCCEEDED(device->CreateBuffer(&cbDesc,&initial,cb.out())),"actual native CB, including bound subranges");
        cbDesc.Usage=D3D11_USAGE_STAGING;cbDesc.BindFlags=0;cbDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(device->CreateBuffer(&cbDesc,nullptr,cbRead.out())),"test-only CB readback");
        auto* ps=logicalBytes==64?ps64.value:ps80.value;
        context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps,nullptr,0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetShaderResources(0,1,&source.source.value);context->PSSetConstantBuffers(0,1,&cb.value);
        UINT offset=offsetBytes/16,count=16;
        if(offsetBytes)extended->PSSetConstantBuffers1(0,1,&cb.value,&offset,&count);
        context->OMSetRenderTargets(1,&native.target.value,nullptr);
        const FLOAT factors[4]={.13f,.27f,.39f,.51f};context->OMSetBlendState(blend.value,factors,sampleMask);
        context->RSSetState(raster.value);
        const D3D11_VIEWPORT viewport{0,0,FLOAT(Width),FLOAT(Height),0,1};context->RSSetViewports(1,&viewport);
        const D3D11_RECT scissor{3,2,LONG(Width-2),LONG(Height-3)};context->RSSetScissorRects(1,&scissor);
        context->CSSetShader(cs.value,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&write.value,nullptr);
        context->CSSetConstantBuffers(0,1,&cb.value);
        const FLOAT clear[4]={0,0,0,0};context->ClearRenderTargetView(native.target.value,clear);
        check(mask.clear(context.value),"owned companion cleared for a new image version");
        context->Draw(3,0);
        const auto before=native.bytes(context.value);const auto originalSource=source.bytes(context.value);
        check(!replay.replay(context.value,wrongSize,ps,logicalBytes,countedDraw,3,0),"extent mismatch is refused");
        check(!replay.replay(context.value,mask,ps==ps64.value?ps80.value:ps64.value,logicalBytes,countedDraw,3,0),"different native program is refused");
        check(!replay.replay(context.value,mask,ps,96,countedDraw,3,0),"unknown constant layout is refused");
        check(!replay.replay(context.value,mask,ps,logicalBytes,countedDraw,0,0),"empty draw is refused");
        D3D11_BLEND_DESC unsupported=blendDesc;unsupported.RenderTarget[0].BlendEnable=TRUE;
        unsupported.RenderTarget[0].SrcBlend=unsupported.RenderTarget[0].DestBlend=D3D11_BLEND_ONE;
        unsupported.RenderTarget[0].SrcBlendAlpha=unsupported.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ONE;
        unsupported.RenderTarget[0].BlendOp=unsupported.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;
        Com<ID3D11BlendState> blended;
        check(SUCCEEDED(device->CreateBlendState(&unsupported,blended.out())),"unsupported native blend fixture");
        context->OMSetBlendState(blended.value,factors,sampleMask);
        check(!replay.replay(context.value,mask,ps,logicalBytes,countedDraw,3,0),"unknown native compositing semantics are refused");
        context->OMSetBlendState(blend.value,factors,sampleMask);
        UINT expectedOffset=0,expectedCount=0;Com<ID3D11Buffer> expectedCB;
        extended->PSGetConstantBuffers1(0,1,expectedCB.out(),&expectedOffset,&expectedCount);
        draws=0;check(replay.replay(context.value,mask,ps,logicalBytes,countedDraw,3,0),"companion alpha replay using actual native state");
        check(draws==1&&replayVertices==3&&!replayStart,"exact original Draw arguments are invoked once for companion only");
        check(replayedConstants!=nullptr,"actual companion uses a distinct cloned GPU CB");
        D3D11_BUFFER_DESC clonedDesc{};replayedConstants->GetDesc(&clonedDesc);
        check(clonedDesc.ByteWidth==logicalBytes,"private GPU clone has exactly the reviewed logical prefix size");
        clonedDesc.Usage=D3D11_USAGE_STAGING;clonedDesc.BindFlags=0;clonedDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Com<ID3D11Buffer> cloneRead;
        check(SUCCEEDED(device->CreateBuffer(&clonedDesc,nullptr,cloneRead.out())),"test-only clone readback");
        context->CopyResource(cloneRead.value,replayedConstants);D3D11_MAPPED_SUBRESOURCE cloneMap{};
        check(SUCCEEDED(context->Map(cloneRead.value,0,D3D11_MAP_READ,0,&cloneMap)),"actual cloned GPU words readback");
        auto expectedWords=words;expectedWords[offsetBytes/4+13]=0x3f800000u;
        check(!std::memcmp(expectedWords.data()+offsetBytes/4,cloneMap.pData,logicalBytes),"GPU clone retains every native bit except alpha policy, including NaN and signed zero");
        context->Unmap(cloneRead.value,0);
        check(before==native.bytes(context.value),"every native RGBA byte remains unchanged");
        check(originalSource==source.bytes(context.value),"source RGB/alpha remain unchanged, including RGB at zero A");
        D3D11_TEXTURE2D_DESC maskDesc{};mask.resource()->GetDesc(&maskDesc);
        maskDesc.Usage=D3D11_USAGE_STAGING;maskDesc.BindFlags=0;maskDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> maskRead;
        check(SUCCEEDED(device->CreateTexture2D(&maskDesc,nullptr,maskRead.out())),"test-only mask readback allocation");
        context->CopyResource(maskRead.value,mask.resource());D3D11_MAPPED_SUBRESOURCE maskMap{};
        check(SUCCEEDED(context->Map(maskRead.value,0,D3D11_MAP_READ,0,&maskMap)),"test-only mask readback");
        std::vector<std::uint8_t> coverage(Width*Height*4);
        for(unsigned y=0;y<Height;++y)std::memcpy(coverage.data()+y*Width*4,
            static_cast<const std::uint8_t*>(maskMap.pData)+y*maskMap.RowPitch,Width*4);
        context->Unmap(maskRead.value,0);
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
            const auto i=y*Width+x;
            const bool inside=(sampleMask&1)&&x>=3&&x<Width-2&&y>=2&&y<Height-3;
            check(coverage[i*4]==0&&coverage[i*4+1]==0&&coverage[i*4+2]==0,"companion exports no RGB");
            check(coverage[i*4+3]==(inside?pixels[i][3]:0),"zero, fractional and opaque A retain exact native viewport/scissor mapping");
            if(inside)check(before[i*4+3]==255,"native path really overwrites alpha with opaque policy");
        }
        context->CopyResource(cbRead.value,cb.value);D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context->Map(cbRead.value,0,D3D11_MAP_READ,0,&mapped)),"test-only constant verification");
        check(!std::memcmp(words.data(),mapped.pData,allocation),"ALL original CB bits remain unchanged");context->Unmap(cbRead.value,0);
        Com<ID3D11PixelShader> actualPS;Com<ID3D11VertexShader> actualVS;Com<ID3D11Buffer> actualCB,computeCB;
        Com<ID3D11RenderTargetView> actualTarget;Com<ID3D11ComputeShader> actualCS;Com<ID3D11UnorderedAccessView> actualUAV;
        Com<ID3D11RasterizerState> actualRaster;Com<ID3D11BlendState> actualBlend;FLOAT actualFactors[4]{};UINT actualSamples=0;
        UINT actualOffset=0,actualCount=0;
        // Legacy CB binding reports its full logical view; ranged binding must
        // retain the original byte window rather than silently rebinding base0.
        context->PSGetShader(actualPS.out(),nullptr,nullptr);context->VSGetShader(actualVS.out(),nullptr,nullptr);
        context->CSGetShader(actualCS.out(),nullptr,nullptr);context->CSGetUnorderedAccessViews(0,1,actualUAV.out());
        context->CSGetConstantBuffers(0,1,computeCB.out());context->RSGetState(actualRaster.out());
        context->OMGetRenderTargets(1,actualTarget.out(),nullptr);context->OMGetBlendState(actualBlend.out(),actualFactors,&actualSamples);
        extended->PSGetConstantBuffers1(0,1,actualCB.out(),&actualOffset,&actualCount);
        check(actualPS.value==ps&&actualVS.value==vs.value&&actualCB.value==cb.value&&actualTarget.value==native.target.value,
            "native graphics bindings restored exactly");
        check(actualOffset==expectedOffset&&actualCount==expectedCount,"native constant buffer range restored exactly");
        check(actualCS.value==cs.value&&actualUAV.value==write.value&&computeCB.value==cb.value,"native CS/UAV/CB state restored by context state swap");
        check(actualRaster.value==raster.value&&actualBlend.value==blend.value&&actualSamples==sampleMask&&
            !std::memcmp(actualFactors,factors,sizeof factors),"native rasterizer, blend factors and sample mask restored");
    }
    context->ClearState();if(replayedConstants){replayedConstants->Release();replayedConstants=nullptr;}
}
}
int main(){try{run(0);run(D3D11_CREATE_DEVICE_SINGLETHREADED);std::printf("PASS %u projected-alpha primitive checks (unwired, not a native frame permit)\n",checks);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,e.what());return 1;}}
