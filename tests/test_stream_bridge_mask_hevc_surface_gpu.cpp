// Actual Windows GPU conversion; no NVENC emulation or headset claim.
#include "pc/stream-bridge-mask-hevc-surface.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace stream_bridge::sender;
namespace {
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class T>struct Hold {T* p=nullptr;~Hold(){if(p)p->Release();}};
void run(ID3D11Device* device,ID3D11DeviceContext* context,UINT width,UINT height){
    std::vector<unsigned char> input(std::size_t(width)*height);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)input[std::size_t(y)*width+x]=static_cast<unsigned char>(x+17*y);
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
    desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8_UNORM;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{input.data(),width,0};Hold<ID3D11Texture2D> source;
    need(SUCCEEDED(device->CreateTexture2D(&desc,&data,&source.p)),"source R8 allocation");
    MaskHevcSurfaces surfaces;need(surfaces.prepareCold(device,source.p),"owned NV12 planar render capability");
    const auto textures=surfaces.textures();need(textures[0]&&textures[3]&&textures[0]!=textures[3],"four independent surfaces");
    const D3D11_VIEWPORT previous{7,9,31,41,0.3f,0.8f};context->RSSetViewports(1,&previous);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_FRONT;raster.ScissorEnable=TRUE;
    Hold<ID3D11RasterizerState> oldRaster;need(SUCCEEDED(device->CreateRasterizerState(&raster,&oldRaster.p)),"native raster fixture");context->RSSetState(oldRaster.p);
    Hold<ID3D11ShaderResourceView> oldResource;need(SUCCEEDED(device->CreateShaderResourceView(source.p,nullptr,&oldResource.p)),"native SRV fixture");context->PSSetShaderResources(0,1,&oldResource.p);
    for(unsigned slot:{0u,3u}){
        need(surfaces.write(context,slot),"owned GPU conversion");
        UINT count=1;D3D11_VIEWPORT restored{};context->RSGetViewports(&count,&restored);
        need(count==1&&restored.TopLeftX==7&&restored.TopLeftY==9&&restored.Width==31&&restored.Height==41&&restored.MinDepth==0.3f&&restored.MaxDepth==0.8f,"native viewport restored");
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);need(topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"native topology restored");
        Hold<ID3D11RasterizerState> observed;context->RSGetState(&observed.p);need(observed.p==oldRaster.p,"native raster restored");
        Hold<ID3D11ShaderResourceView> resource;context->PSGetShaderResources(0,1,&resource.p);need(resource.p==oldResource.p,"native SRV restored");
        textures[slot]->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Hold<ID3D11Texture2D> staging;need(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging.p)),"NV12 readback allocation");
        context->CopyResource(staging.p,textures[slot]);D3D11_MAPPED_SUBRESOURCE mapped{};
        // Blocking readback exists only in this verification fixture.
        need(SUCCEEDED(context->Map(staging.p,0,D3D11_MAP_READ,0,&mapped)),"test readback completion");
        need(mapped.pData&&mapped.RowPitch>=width,"NV12 owned stride");
        const auto* bytes=static_cast<const unsigned char*>(mapped.pData);
        bool exact=true;
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)
            exact&=bytes[std::size_t(y)*mapped.RowPitch+x]==255-input[std::size_t(y)*width+x];
        for(UINT y=0;y<height/2;++y)for(UINT x=0;x<width;++x)
            exact&=bytes[std::size_t(height+y)*mapped.RowPitch+x]==128;
        context->Unmap(staging.p,0);need(exact,"all256 alpha codes full-range and neutral UV exact");
    }
    need(!surfaces.write(context,4),"invalid slot refused");
    surfaces.resetCold();need(!surfaces.write(context,0),"retired surface refused");
    context->ClearState();context->Flush();
}
}
int main(){try{
    Hold<ID3D11Device> device;Hold<ID3D11DeviceContext> context;
    need(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device.p,nullptr,&context.p)),"actual WARP D3D11 device");
    run(device.p,context.p,256,8);run(device.p,context.p,1152,4608);
    std::puts("Owned full-size R8-to-NV12 GPU conversion and native pipeline restoration PASS");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"Mask HEVC surface test failed: %s\n",e.what());return 1;}}
