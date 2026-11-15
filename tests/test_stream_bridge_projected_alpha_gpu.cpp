#include "pc/stream-bridge-projected-alpha-replay.h"
#include "pc/stream-bridge-projected-alpha-sync.h"
#include "pc/stream-bridge-projected-alpha-kernels.h"
#include "pc/stream-bridge-projected-alpha-textures.h"
#include "pc/stream-bridge-projected-alpha-channel.h"
#include "pc/stream-bridge-projected-alpha-geometry.h"
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
const char* exportDirectory=nullptr;
unsigned nativeAcquires=0,nativeReleases=0,completedReleases=0;
bool eventInvalid=false;
unsigned textureCreates=0,textureOpens=0,nestedTextureCreates=0,textureTargets=0;
bool nestedTextureFailure=false;
ID3D11Device* lastTextureDevice=nullptr;
Geometry* activeGeometry=nullptr;ID3D11Device* geometryDevice=nullptr;
void targetObserved(ID3D11Device*,ID3D11RenderTargetView*,void*)noexcept{++textureTargets;}
void bufferObserved(ID3D11Device* device,const D3D11_BUFFER_DESC* desc,const D3D11_SUBRESOURCE_DATA* data,ID3D11Buffer* buffer,void*)noexcept{
    if(activeGeometry&&device==geometryDevice)activeGeometry->buffer(desc,data,buffer);
}
void layoutObserved(ID3D11Device* device,const D3D11_INPUT_ELEMENT_DESC* desc,UINT count,const void* code,SIZE_T size,ID3D11InputLayout* layout,void*)noexcept{
    if(activeGeometry&&device==geometryDevice)activeGeometry->layout(desc,count,code,size,layout);
}
void textureObserved(ID3D11Device* device,ID3D11Texture2D*,TextureOrigin origin,void*)noexcept{
    lastTextureDevice=device;
    if(origin==TextureOrigin::Created)++textureCreates;else ++textureOpens;
    if(!nestedTextureCreates){
        ++nestedTextureCreates;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        ID3D11Texture2D* own=nullptr;
        nestedTextureFailure=FAILED(device->CreateTexture2D(&desc,nullptr,&own));if(own)own->Release();
    }
}
void acquired(const MutexLease& lease,void*)noexcept{++nativeAcquires;eventInvalid|=!bool(lease);}
void releasing(const MutexLease& lease,std::uint64_t,void*)noexcept{++nativeReleases;eventInvalid|=!bool(lease);}
void released(const MutexLease& lease,HRESULT result,void*)noexcept{++completedReleases;eventInvalid|=!bool(lease)||result!=S_OK;}
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
float4 quad(uint id:SV_VertexID):SV_Position {
    return float4(id>=2?1:-1,(id&1)?1:-1,0,1);
}
float4 unitQuad(float2 uv:TEXCOORD0):SV_Position {return float4(uv*2-1,0,1);}
float4 white():SV_Target {return float4(1,0,0,1);}
float4 copyRgba(float4 position:SV_Position):SV_Target {
    float4 value=source.Load(int3(int2(position.xy),0));
    value.rgb*=parameters[1].xyz;
    return value;
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
    Com<ID3D11Texture2D> image,read,resolved;
    Com<ID3D11RenderTargetView> target;
    Com<ID3D11ShaderResourceView> source;
    bool prepare(ID3D11Device* device,UINT width,UINT height,UINT misc=0,UINT samples=1){
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=misc;
        desc.SampleDesc.Count=samples;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,image.out()))||FAILED(device->CreateRenderTargetView(image.value,nullptr,target.out()))||
            FAILED(device->CreateShaderResourceView(image.value,nullptr,source.out())))return false;
        desc.SampleDesc.Count=1;desc.MiscFlags=0;
        if(samples!=1&&FAILED(device->CreateTexture2D(&desc,nullptr,resolved.out())))return false;
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(device->CreateTexture2D(&desc,nullptr,read.out()));
    }
    std::vector<std::uint8_t> bytes(ID3D11DeviceContext* context)const{
        D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);
        if(desc.SampleDesc.Count!=1)context->ResolveSubresource(resolved.value,0,image.value,0,desc.Format);
        context->CopyResource(read.value,resolved.value?resolved.value:image.value);
        D3D11_MAPPED_SUBRESOURCE map{};
        check(SUCCEEDED(context->Map(read.value,0,D3D11_MAP_READ,0,&map)),"test-only owned image readback");
        std::vector<std::uint8_t> result(desc.Width*desc.Height*4);
        for(UINT y=0;y<desc.Height;++y)std::memcpy(result.data()+y*desc.Width*4,
            static_cast<const std::uint8_t*>(map.pData)+y*map.RowPitch,desc.Width*4);
        context->Unmap(read.value,0);return result;
    }
};
constexpr char ResolveFixture[]=R"(
cbuffer Policy : register(b2) { float4 flags[2]; };
Texture2DMS<float4,4> multisample : register(t0);
float4 resolve(float4 position:SV_Position):SV_Target {
    float4 average=0;
    [unroll] for(uint sampleIndex=0;sampleIndex<4;++sampleIndex) average+=multisample.Load(int2(position.xy),sampleIndex)*0.25;
    if(flags[0].x>0) average.a=1;
    return average;
})";
void multisample(ID3D11Device* device,ID3D11DeviceContext* context,Replay& replay,
    ID3D11VertexShader* quad,ID3D11PixelShader* scene,const Texture& source){
    D3D11_TEXTURE2D_DESC sourceDesc{};source.image->GetDesc(&sourceDesc);
    const auto width=sourceDesc.Width,height=sourceDesc.Height;
    Texture nativeMsaa,referenceMsaa,nativeFinal,referenceFinal;
    Image alphaMsaa,alphaFinal;
    check(nativeMsaa.prepare(device,width,height,0,4)&&referenceMsaa.prepare(device,width,height,0,4)&&
        nativeFinal.prepare(device,width,height)&&referenceFinal.prepare(device,width,height)&&
        alphaMsaa.prepare(device,width,height,false,4)&&alphaFinal.prepare(device,width,height,true),"distinct native and private four-sample images");
    auto resolveCode=compile(ResolveFixture,"resolve","ps_5_0");Com<ID3D11PixelShader> resolve;
    check(SUCCEEDED(device->CreatePixelShader(resolveCode->GetBufferPointer(),resolveCode->GetBufferSize(),nullptr,resolve.out())),"owned native resolve-policy model");
    D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=64;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    std::array<float,16> values{};values[4]=values[5]=values[6]=1;
    D3D11_SUBRESOURCE_DATA initial{values.data(),0,0};Com<ID3D11Buffer> ordinary,preserved;
    check(SUCCEEDED(device->CreateBuffer(&cbDesc,&initial,ordinary.out())),"native scene opaque-alpha constants");
    values[13]=1;
    check(SUCCEEDED(device->CreateBuffer(&cbDesc,&initial,preserved.out())),"independent reference scene constants");
    Com<ID3D11DeviceContext1> extended;
    check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"MSAA context1");
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;
    rasterDesc.MultisampleEnable=TRUE;rasterDesc.DepthClipEnable=TRUE;
    Com<ID3D11RasterizerState> raster;check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,raster.out())),"actual native MSAA rasterizer");
    context->RSSetState(raster.value);context->VSSetShader(quad,nullptr,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->OMSetBlendState(nullptr,nullptr,0xbu);
    const D3D11_VIEWPORT sceneViewport{.25f,.5f,float(width)-1,float(height)-1,0,1};context->RSSetViewports(1,&sceneViewport);
    const FLOAT opaque[4]={0,0,0,1},zero[4]{};
    context->ClearRenderTargetView(nativeMsaa.target.value,opaque);context->ClearRenderTargetView(referenceMsaa.target.value,zero);
    check(alphaMsaa.clear(context),"all private coverage samples start at zero");
    context->PSSetShader(scene,nullptr,0);context->PSSetShaderResources(0,1,&source.source.value);
    context->PSSetConstantBuffers(0,1,&preserved.value);context->OMSetRenderTargets(1,&referenceMsaa.target.value,nullptr);context->Draw(4,0);
    context->PSSetConstantBuffers(0,1,&ordinary.value);context->OMSetRenderTargets(1,&nativeMsaa.target.value,nullptr);context->Draw(4,0);
    const auto nativeBefore=nativeMsaa.bytes(context);
    check(replay.replay(context,alphaMsaa,scene,64,countedDraw,4,0),"alpha-only replay retains all four native sample positions and coverage");
    check(nativeMsaa.bytes(context)==nativeBefore,"every resolved native RGBA byte is unchanged after MSAA companion extraction");
    const D3D11_VIEWPORT resolveViewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&resolveViewport);
    context->PSSetShader(resolve.value,nullptr,0);context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    for(UINT byteOffset:{0u,256u}){
        const UINT size=byteOffset?512:32;std::vector<std::uint32_t> words(size/4,0x7fc12345u);
        for(unsigned i=0;i<8;++i)words[byteOffset/4+i]=0;
        cbDesc.ByteWidth=size;D3D11_SUBRESOURCE_DATA policyData{words.data(),0,0};Com<ID3D11Buffer> policy;
        check(SUCCEEDED(device->CreateBuffer(&cbDesc,&policyData,policy.out())),"exact native resolve b2, including a ranged view");
        context->PSSetConstantBuffers(2,1,&policy.value);UINT offset=byteOffset/16,count=16;
        if(byteOffset)extended->PSSetConstantBuffers1(2,1,&policy.value,&offset,&count);
        context->PSSetShaderResources(0,1,&referenceMsaa.source.value);
        context->OMSetRenderTargets(1,&referenceFinal.target.value,nullptr);context->Draw(4,0);
        const auto expected=referenceFinal.bytes(context);
        words[byteOffset/4]=0x3f800000u;context->UpdateSubresource(policy.value,0,nullptr,words.data(),0,0);
        context->PSSetShaderResources(0,1,&nativeMsaa.source.value);
        context->OMSetRenderTargets(1,&nativeFinal.target.value,nullptr);context->Draw(4,0);
        const auto before=nativeFinal.bytes(context);
        check(alphaFinal.clear(context)&&replay.resolve(context,alphaFinal,alphaMsaa,resolve.value,countedDraw,4,0),"native four-sample resolve produces companion alpha without its opaque-output policy");
        check(before==nativeFinal.bytes(context),"normal native resolve result stays byte-identical");
        context->CopyResource(referenceFinal.image.value,alphaFinal.resource());const auto actual=referenceFinal.bytes(context);
        unsigned transparent=0,fractional=0;
        for(unsigned i=0;i<width*height;++i){
            check(!actual[i*4]&&!actual[i*4+1]&&!actual[i*4+2],"resolved coverage contains no scene RGB");
            check(actual[i*4+3]==expected[i*4+3],"four-sample scene and resolve preserve the exact reference coverage at fractional viewport edges");
            check(before[i*4+3]==255,"actual normal resolve policy hides all source alpha");
            transparent+=actual[i*4+3]==0;fractional+=actual[i*4+3]>0&&actual[i*4+3]<255;
        }
        check(transparent&&fractional,"MSAA control really exercises holes, fractional alpha and subpixel geometry");
        Com<ID3D11ShaderResourceView> originalSource;context->PSGetShaderResources(0,1,originalSource.out());
        Com<ID3D11Buffer> originalPolicy;UINT originalOffset=0,originalCount=0;
        extended->PSGetConstantBuffers1(2,1,originalPolicy.out(),&originalOffset,&originalCount);
        check(originalSource.value==nativeMsaa.source.value&&originalPolicy.value==policy.value&&originalOffset==offset,
            "original native multisample input and b2 range restored");
    }
}
std::uint64_t sharedHandle(ID3D11Texture2D* texture){
    Com<IDXGIResource> shared;HANDLE handle=nullptr;
    check(SUCCEEDED(texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))&&
        SUCCEEDED(shared->GetSharedHandle(&handle))&&handle,"real kernel shared-allocation handle");
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(handle));
}
void exportKernel(const char* name,ID3DBlob* code){
    if(!exportDirectory)return;
    char path[1024]{};const auto written=std::snprintf(path,sizeof path,"%s/%s",exportDirectory,name);
    check(written>0&&static_cast<std::size_t>(written)<sizeof path,"bounded owned kernel output path");
#ifdef _MSC_VER
    FILE* out=nullptr;check(fopen_s(&out,path,"wb")==0&&out,"owned kernel output");
#else
    FILE* out=std::fopen(path,"wb");check(out!=nullptr,"owned kernel output");
#endif
    const bool okay=std::fwrite(code->GetBufferPointer(),1,code->GetBufferSize(),out)==code->GetBufferSize();
    check(std::fclose(out)==0&&okay,"complete owned kernel bytes");
}
void geometryControl(ID3D11Device* device,ID3D11DeviceContext* context,Geometry& geometry){
    auto vsCode=compile(Fixture,"unitQuad","vs_5_0"),psCode=compile(Fixture,"white","ps_5_0");
    Com<ID3D11VertexShader> vs;Com<ID3D11PixelShader> ps;Com<ID3D11InputLayout> layout;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out())),"owned register0 unit-quad geometry model");
    const D3D11_INPUT_ELEMENT_DESC element{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
    check(SUCCEEDED(device->CreateInputLayout(&element,1,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.out())),"actual native-style layout reflected by creation callback");
    const std::array<float,16> points{{4,4,4,5,5,4,5,5, 0,0,0,1,1,0,1,1}};
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof points;desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA input{points.data(),0,0};Com<ID3D11Buffer> buffer;
    check(SUCCEEDED(device->CreateBuffer(&desc,&input,buffer.out())),"actual vertex bytes retained from public immutable creation data");
    context->IASetInputLayout(layout.value);UINT stride=8,offset=0;context->IASetVertexBuffers(0,1,&buffer.value,&stride,&offset);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps.value,nullptr,0);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);
    D3D11_RASTERIZER_DESC rasterDesc{};rasterDesc.FillMode=D3D11_FILL_SOLID;rasterDesc.CullMode=D3D11_CULL_NONE;rasterDesc.DepthClipEnable=TRUE;
    Com<ID3D11RasterizerState> raster;check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,raster.out())),"owned geometry rasterizer");context->RSSetState(raster.value);
    constexpr UINT width=37,height=29;
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    Texture output;check(output.prepare(device,width,height),"owned geometry reference output");context->OMSetRenderTargets(1,&output.target.value,nullptr);
    check(geometry.fullUnitQuad(context,4,4,width,height,1),"full overwrite is proved from the actual nonzero vertex range and bound layout");
    const FLOAT zero[4]{};context->ClearRenderTargetView(output.target.value,zero);context->Draw(4,4);const auto full=output.bytes(context);
    for(unsigned i=0;i<width*height;++i)check(full[i*4+3]==255,"real GPU confirms every pixel of the metadata-proved quad");
    geometry.invalidate();
    check(geometry.fullUnitQuad(context,4,4,width,height,1),"unknown mutable-resource writes retain immutable vertex creation evidence");
    check(!geometry.fullUnitQuad(context,4,0,width,height,1),"same count/topology cannot certify the offscreen vertex prefix");
    context->ClearRenderTargetView(output.target.value,zero);context->Draw(4,0);const auto offscreen=output.bytes(context);
    for(unsigned i=0;i<width*height;++i)check(!offscreen[i*4+3],"wrong start is a causal all-offscreen geometry control");
    rasterDesc.CullMode=D3D11_CULL_BACK;Com<ID3D11RasterizerState> culled;
    check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,culled.out())),"native back-face culling control");context->RSSetState(culled.value);
    check(geometry.fullUnitQuad(context,4,4,width,height,1),"front-face winding is included in overwrite admission");
    context->ClearRenderTargetView(output.target.value,zero);context->Draw(4,4);const auto front=output.bytes(context);
    for(unsigned i=0;i<width*height;++i)check(front[i*4+3]==255,"real GPU agrees with admitted clockwise front-face coverage");
    rasterDesc.FrontCounterClockwise=TRUE;Com<ID3D11RasterizerState> wrongFace;
    check(SUCCEEDED(device->CreateRasterizerState(&rasterDesc,wrongFace.out())),"opposite front-face fixture");context->RSSetState(wrongFace.value);
    check(!geometry.fullUnitQuad(context,4,4,width,height,1),"culled quad cannot repair unknown image content");
    context->RSSetState(raster.value);
    desc.ByteWidth=32;desc.Usage=D3D11_USAGE_DYNAMIC;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;input.pSysMem=points.data()+8;
    Com<ID3D11Buffer> dynamic;check(SUCCEEDED(device->CreateBuffer(&desc,&input,dynamic.out())),"actual small native mapped vertex buffer");
    context->IASetVertexBuffers(0,1,&dynamic.value,&stride,&offset);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(dynamic.value,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)),"real native vertex map");geometry.beginMap(dynamic.value,mapped.pData);
    const std::array<float,8> partial{{0,0,0,.5f,.5f,0,.5f,.5f}};std::memcpy(mapped.pData,partial.data(),sizeof partial);
    geometry.beforeUnmap(dynamic.value);context->Unmap(dynamic.value,0);
    check(!geometry.fullUnitQuad(context,4,0,width,height,1),"native mapped changes cannot reuse old full-quad metadata");
    check(SUCCEEDED(context->Map(dynamic.value,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)),"real native full-quad restore map");geometry.beginMap(dynamic.value,mapped.pData);
    std::memcpy(mapped.pData,points.data()+8,32);geometry.beforeUnmap(dynamic.value);context->Unmap(dynamic.value,0);
    check(geometry.fullUnitQuad(context,4,0,width,height,1),"fresh public Unmap metadata restores only the actual whole quad");
}
void intermediateControl(ID3D11Device* device,ID3D11DeviceContext* context,Replay& replay,
    ID3D11VertexShader* quad,ID3D11PixelShader* scene,const Texture& source){
    D3D11_TEXTURE2D_DESC desc{};source.image->GetDesc(&desc);const auto width=desc.Width,height=desc.Height;
    desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
    Com<ID3D11Texture2D> native,read;Com<ID3D11RenderTargetView> target;Com<ID3D11ShaderResourceView> view;
    check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out()))&&SUCCEEDED(device->CreateRenderTargetView(native.value,nullptr,target.out()))&&
        SUCCEEDED(device->CreateShaderResourceView(native.value,nullptr,view.out())),"native-style FP16 intermediate");
    desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.BindFlags=0;
    check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,read.out())),"test-only native FP16 readback");
    const auto bytes=[&](ID3D11Texture2D* texture){
        context->CopyResource(read.value,texture);D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context->Map(read.value,0,D3D11_MAP_READ,0,&mapped)),"owned FP16 readback");
        std::vector<std::uint8_t> result(width*height*8);
        for(UINT y=0;y<height;++y)std::memcpy(result.data()+y*width*8,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,width*8);
        context->Unmap(read.value,0);return result;
    };
    Image shadow,mask;
    check(shadow.prepare(device,width,height,false,1,0,DXGI_FORMAT_R16G16B16A16_FLOAT)&&mask.prepare(device,width,height),"private FP16 RGBA shadow and separate final alpha image");
    auto copyCode=compile(Fixture,"copyRgba","ps_5_0");Com<ID3D11PixelShader> copy;
    check(SUCCEEDED(device->CreatePixelShader(copyCode->GetBufferPointer(),copyCode->GetBufferSize(),nullptr,copy.out())),"owned X-stage RGBA model");
    std::array<float,16> constants{};constants[4]=.5f;constants[5]=.75f;constants[6]=.875f;
    D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=sizeof constants;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA initial{constants.data(),0,0};Com<ID3D11Buffer> cb;
    check(SUCCEEDED(device->CreateBuffer(&cbDesc,&initial,cb.out())),"actual native model constants");
    context->VSSetShader(quad,nullptr,0);context->PSSetShader(copy.value,nullptr,0);context->PSSetConstantBuffers(0,1,&cb.value);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);context->PSSetShaderResources(0,1,&source.source.value);
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    context->OMSetBlendState(nullptr,nullptr,0xffffffffu);context->OMSetRenderTargets(1,&target.value,nullptr);
    const FLOAT zero[4]{};context->ClearRenderTargetView(target.value,zero);check(shadow.clear(context),"private intermediate starts empty");
    context->Draw(4,0);const auto before=bytes(native.value);
    check(replay.intermediate(context,shadow,copy.value,countedDraw,4,0),"native X-stage draw copied to a distinct FP16 shadow without a policy override");
    check(bytes(native.value)==before&&bytes(shadow.resource())==before,"every native FP16 word is unchanged and private RGBA shadow matches exactly");
    Texture output,coverage;check(output.prepare(device,width,height)&&coverage.prepare(device,width,height),"owned Y-stage output and coverage verification");
    context->PSSetShader(scene,nullptr,0);context->OMSetRenderTargets(1,&output.target.value,nullptr);context->PSSetShaderResources(0,1,&view.value);
    context->Draw(4,0);const auto original=output.bytes(context);
    check(mask.clear(context)&&replay.filtered(context,mask,shadow,scene,64,countedDraw,4,0),"Y-stage alpha uses the matching private FP16 ancestry with unchanged native geometry");
    check(output.bytes(context)==original,"original opaque Y-stage RGB/alpha remains byte-identical");
    context->CopyResource(coverage.image.value,mask.resource());const auto alpha=coverage.bytes(context),inputBytes=source.bytes(context);
    for(UINT i=0;i<width*height;++i){
        check(!alpha[i*4]&&!alpha[i*4+1]&&!alpha[i*4+2],"only alpha crosses the private intermediate boundary");
        check(alpha[i*4+3]==inputBytes[i*4+3],"FP16 source zero/fractional/opaque alpha survives the normal native Y output-opacity policy");
    }
    Com<ID3D11ShaderResourceView> restored;context->PSGetShaderResources(0,1,restored.out());
    check(restored.value==view.value,"original native FP16 source restored after companion replay");
}
void run(UINT creationFlags,MutexObserver& mutexes,TextureObserver& textures){
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL requested[]={D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,creationFlags,requested,2,
        D3D11_SDK_VERSION,device.out(),&level,context.out())),"actual Windows WARP device");
    Com<ID3D11DeviceContext1> extended;
    check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"actual immediate context1");
    Geometry geometry;activeGeometry=&geometry;geometryDevice=device.value;
    check(mutexes.prepare(device.value),"cold native keyed-mutex observation on the owning device thread");
    auto** const originalDeviceTable=*reinterpret_cast<void***>(device.value);
    check(textures.prepare(device.value)&&*reinterpret_cast<void***>(device.value)==originalDeviceTable,
        "texture preparation preserves the complete original native device vptr and private tail");
    auto clone64=compile(Clone64Hlsl,"main","cs_5_0"),clone80=compile(Clone80Hlsl,"main","cs_5_0");
    auto clone32=compile(CloneResolve32Hlsl,"main","cs_5_0");
    exportKernel("projected-alpha-clone64.dxbc",clone64.value);exportKernel("projected-alpha-clone80.dxbc",clone80.value);
    exportKernel("projected-alpha-clone32.dxbc",clone32.value);
    Replay replay;
    const Bytecode code64=exportDirectory?Bytecode{clone64->GetBufferPointer(),clone64->GetBufferSize()}:Bytecode{kernels::Clone64.data(),kernels::Clone64.size()};
    const Bytecode code80=exportDirectory?Bytecode{clone80->GetBufferPointer(),clone80->GetBufferSize()}:Bytecode{kernels::Clone80.data(),kernels::Clone80.size()};
    const Bytecode code32=exportDirectory?Bytecode{clone32->GetBufferPointer(),clone32->GetBufferSize()}:Bytecode{kernels::Clone32.data(),kernels::Clone32.size()};
    check(replay.prepare(device.value,code64,code80,code32),"cold bounded clone/state/blend preparation");
    auto vsCode=compile(Fixture,"vertex","vs_5_0"),quadCode=compile(Fixture,"quad","vs_5_0"),ps64Code=compile(Fixture,"pixel64","ps_5_0"),
        ps80Code=compile(Fixture,"pixel","ps_5_0"),csCode=compile(Fixture,"compute","cs_5_0");
    Com<ID3D11VertexShader> vs,quad;Com<ID3D11PixelShader> ps64,ps80;Com<ID3D11ComputeShader> cs;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreateVertexShader(quadCode->GetBufferPointer(),quadCode->GetBufferSize(),nullptr,quad.out()))&&
        SUCCEEDED(device->CreatePixelShader(ps64Code->GetBufferPointer(),ps64Code->GetBufferSize(),nullptr,ps64.out()))&&
        SUCCEEDED(device->CreatePixelShader(ps80Code->GetBufferPointer(),ps80Code->GetBufferSize(),nullptr,ps80.out()))&&
        SUCCEEDED(device->CreateComputeShader(csCode->GetBufferPointer(),csCode->GetBufferSize(),nullptr,cs.out())),"owned fixture programs");
    Texture source,native;
    Image mask,wrongSize;
    constexpr UINT Width=37,Height=29;
    check(source.prepare(device.value,Width,Height)&&native.prepare(device.value,Width,Height,D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)&&
        mask.prepare(device.value,Width,Height,true)&&wrongSize.prepare(device.value,Width+1,Height),"owned non-square shared coverage and native RGB images");
    const auto nativeHandle=sharedHandle(native.image.value),maskHandle=sharedHandle(mask.resource());
    Com<IDXGIKeyedMutex> producerMutex,consumerMutex;
    check(SUCCEEDED(native.image->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(producerMutex.out()))),"actual native producer mutex");
    Com<ID3D11Device> consumer;Com<ID3D11DeviceContext> consume;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,requested,2,
        D3D11_SDK_VERSION,consumer.out(),nullptr,consume.out()))&&mutexes.prepare(consumer.value)&&textures.prepare(consumer.value),"independent actual WARP consumer device");
    Com<ID3D11Texture2D> importedNative,importedMask;
    const auto opensBefore=textureOpens;
    check(SUCCEEDED(consumer->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(nativeHandle)),
        __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(importedNative.out())))&&
        SUCCEEDED(consumer->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(maskHandle)),
        __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(importedMask.out()))),"independently opened RGB and alpha allocations");
    check(textureOpens==opensBefore+2&&nestedTextureCreates==1&&!nestedTextureFailure,
        "actual native shared opens are observed once and companion allocation never recurses into native ancestry");
    {
        ChannelParent channel;check(channel.create(),"private retained-source publication fixture");
        Registry registry{};registry.sourceLease=0x70000004du;registry.revision=1;
        registry.retained[0]={nativeHandle,13,77,Width,Height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
        std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=native.image.value;
        channel.sourceLease(registry.sourceLease);
        check(channel.publishRetained(registry,pins),"published metadata independently matches a real pinned kernel texture");
        RegistryRead held;check(held.acquire(channel.page())&&held.snapshot().adapterKnown,"reader pins its source snapshot and physical adapter");
        registry.revision=2;
        check(!channel.publishRetained(registry,pins),"publisher never releases/recycles old kernel pins while a source admission reads them");
        held.release();check(channel.publishRetained(registry,pins),"publication resumes without waiting after the reader retains its source binding");
        registry.retained[0].handle^=1;
        check(!channel.publishRetained(registry,pins),"same-shaped foreign numeric allocation identity cannot be published");
        channel.sourceLease(0x80000004du);
        check(!held.acquire(channel.page()),"source revocation rejects old format metadata immediately, before the next cold refresh");
    }
    check(SUCCEEDED(importedNative->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(consumerMutex.out()))),"actual native consumer mutex");
    Texture receivedNative,receivedMask;
    check(receivedNative.prepare(consumer.value,Width,Height)&&receivedMask.prepare(consumer.value,Width,Height),"owned consumer verification storage");
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
    for(bool strip:{false,true})for(unsigned logicalBytes:{64u,80u})for(unsigned offsetBytes:{0u,256u})for(UINT sampleMask:{0x12345679u,0x12345678u}){
        check(producerMutex->AcquireSync(0,0)==S_OK,"original native producer acquire");
        const auto producerLease=mutexes.current(device.value,nativeHandle);
        check(bool(producerLease)&&producerLease.key==0,"successful actual native acquire creates a versioned allocation lease");
        std::array<MutexLease,8> currentScopes{};const auto scopes=mutexes.capture(device.value,currentScopes);
        check(scopes==1&&currentScopes[0].epoch==producerLease.epoch&&currentScopes[0].retained,
            "capture names the actual retained native mutex scope, not an inferred presentation");
        const auto beforeTimeout=nativeAcquires;
        check(consumerMutex->AcquireSync(0,0)==HRESULT(WAIT_TIMEOUT),"original native contended acquire reports timeout");
        check(!mutexes.current(consumer.value,nativeHandle)&&nativeAcquires==beforeTimeout,"positive WAIT_TIMEOUT HRESULT never creates a consumer lease");
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
        auto* vertex=strip?quad.value:vs.value;const UINT vertices=strip?4:3;
        const auto topology=strip?D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        context->VSSetShader(vertex,nullptr,0);context->PSSetShader(ps,nullptr,0);
        context->IASetPrimitiveTopology(topology);
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
        context->Draw(vertices,0);
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
        draws=0;check(replay.replay(context.value,mask,ps,logicalBytes,countedDraw,vertices,0),"companion alpha replay using actual native state");
        check(draws==1&&replayVertices==vertices&&!replayStart,"exact original Draw arguments are invoked once for companion only");
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
        check(actualPS.value==ps&&actualVS.value==vertex&&actualCB.value==cb.value&&actualTarget.value==native.target.value,
            "native graphics bindings restored exactly");
        check(actualOffset==expectedOffset&&actualCount==expectedCount,"native constant buffer range restored exactly");
        check(actualCS.value==cs.value&&actualUAV.value==write.value&&computeCB.value==cb.value,"native CS/UAV/CB state restored by context state swap");
        D3D11_PRIMITIVE_TOPOLOGY actualTopology{};context->IAGetPrimitiveTopology(&actualTopology);
        check(actualTopology==topology,"native three-vertex list and four-vertex strip topologies remain unchanged");
        check(actualRaster.value==raster.value&&actualBlend.value==blend.value&&actualSamples==sampleMask&&
            !std::memcmp(actualFactors,factors,sizeof factors),"native rasterizer, blend factors and sample mask restored");
        context->Flush();
        check(producerMutex->ReleaseSync(0)==S_OK&&!mutexes.current(device.value,nativeHandle),"original native release ends only its exact producer lease");
        check(consumerMutex->AcquireSync(0,2000)==S_OK,"original native consumer acquire establishes cross-device order");
        const auto consumerLease=mutexes.current(consumer.value,nativeHandle);
        check(bool(consumerLease)&&consumerLease.epoch!=producerLease.epoch&&consumerLease.allocation==producerLease.allocation,
            "same physical sync allocation has distinct observed producer and consumer ownership scopes");
        consume->CopyResource(receivedNative.image.value,importedNative.value);consume->CopyResource(receivedMask.image.value,importedMask.value);
        check(receivedNative.bytes(consume.value)==before,"native RGB/alpha bytes cross the real shared-mutex transfer unchanged");
        check(receivedMask.bytes(consume.value)==coverage,"exact companion alpha crosses the SAME native ownership transfer");
        consume->Flush();check(consumerMutex->ReleaseSync(0)==S_OK&&!mutexes.current(consumer.value,nativeHandle),"consumer releases actual ownership after copying the current pair");
    }
    check(!eventInvalid&&nativeAcquires==nativeReleases&&nativeReleases==completedReleases,"only successful native lock scopes reach bounded callbacks");
    multisample(device.value,context.value,replay,quad.value,ps64.value,source);
    intermediateControl(device.value,context.value,replay,quad.value,ps64.value,source);
    geometryControl(device.value,context.value,geometry);
    context->ClearState();if(replayedConstants){replayedConstants->Release();replayedConstants=nullptr;}
    activeGeometry=nullptr;geometryDevice=nullptr;
}
}
int main(int argc,char** argv){try{
    if(argc==3&&!std::strcmp(argv[1],"--export-owned-kernels"))exportDirectory=argv[2];else check(argc==1,"only explicit owned-kernel export is accepted");
    MutexObserver mutexes({acquired,releasing,released,nullptr});
    TextureObserver textures(textureObserved,nullptr,targetObserved,bufferObserved,layoutObserved);
    run(0,mutexes,textures);run(D3D11_CREATE_DEVICE_SINGLETHREADED,mutexes,textures);
    check(textureCreates&&textureTargets&&textureOpens==4&&lastTextureDevice,"bounded native texture/view callbacks executed on both original devices");
    using CreateTexture=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const D3D11_TEXTURE2D_DESC*,const D3D11_SUBRESOURCE_DATA*,ID3D11Texture2D**);
    CreateTexture cached=nullptr;auto* entry=(*reinterpret_cast<void***>(lastTextureDevice))[5];std::memcpy(&cached,&entry,sizeof cached);
    textures.stop();const auto beforeStop=textureCreates;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    Com<ID3D11Texture2D> afterStop;
    check(SUCCEEDED(cached(lastTextureDevice,&desc,nullptr,afterStop.out()))&&textureCreates==beforeStop,
        "cached texture callback forwards safely after stop without declaring new ancestry");
    std::printf("PASS %u projected-alpha primitive checks, %u native mutex transfers (unwired, not a native frame permit)\n",checks,completedReleases);return 0;}
    catch(const std::exception& e){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,e.what());return 1;}}
