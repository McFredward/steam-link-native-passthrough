// Included inside the context fixture's anonymous namespace. All shaders,
// handles and NVENC ports below are owned synthetic inputs on an actual WARP
// device; accepted fake submissions do not claim hardware encoding or a peer.
struct CompositionFixture {
    static constexpr UINT Width=64,Height=64;
    enum class Mode {Valid,MissingTile,WrongEye,ClearImage,WrongChroma,WrongLuma,UnknownDraw,UnrelatedWrite,RegionWrite,DiscardImage,
        DirectUV,DirectUVLumaFirst,DirectUVWrongSource,DirectUVWrongOutput,DirectUVRepeated,DirectUVMixed};
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;Mode mode=Mode::Valid;unsigned created=0,eyeBase=0;
    DXGI_FORMAT planarFormat=DXGI_FORMAT_NV12;
    NV_ENC_BUFFER_FORMAT encoderFormat()const{return planarFormat==DXGI_FORMAT_P010?NV_ENC_BUFFER_FORMAT_YUV420_10BIT:NV_ENC_BUFFER_FORMAT_NV12;}
    std::array<std::array<Com<ID3D11Texture2D>,3>,4> sources;
    std::array<Com<ID3D11ShaderResourceView>,4> eyes;
    Com<ID3D11Texture2D> image,chroma,foreignChroma,planar,foreignPlanar,input,unrelated,readback;
    Com<ID3D11RenderTargetView> imageTarget,chromaTarget,uvTarget,yTarget,foreignY,foreignUV,unrelatedTarget;
    Com<ID3D11ShaderResourceView> imageView,chromaView,foreignChromaView;
    Com<ID3D11VertexShader> vertex;Com<ID3D11PixelShader> copyPixel,chromaPixel,packPixel,lumaPixel,unknownPixel;
    Com<ID3D11Buffer> constants;Com<ID3D11RasterizerState> raster;
    void** nativeTable=nullptr;bool bypass=false;
    std::array<std::vector<std::uint8_t>,3> nativeReferences;
    using NativeDraw=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,UINT,UINT);
    using NativeCopy=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*);
    void draw(){if(bypass)pointerBits<NativeDraw>(nativeTable[13])(context,3,0);else context->Draw(3,0);}
    void copy(ID3D11Resource* destination,ID3D11Resource* source){
        if(bypass)pointerBits<NativeCopy>(nativeTable[47])(context,destination,source);else context->CopyResource(destination,source);
    }
    std::vector<std::uint8_t> output(){
        pointerBits<NativeCopy>(nativeTable[47])(context,readback.p,planar.p);D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context->Map(readback.p,0,D3D11_MAP_READ,0,&mapped)),"owned native planar comparison map");
        const UINT rowBytes=Width*(planarFormat==DXGI_FORMAT_P010?2u:1u);
        std::vector<std::uint8_t> bytes(rowBytes*Height*3/2);
        for(UINT y=0;y<Height*3/2;++y)std::memcpy(bytes.data()+y*rowBytes,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,rowBytes);
        context->Unmap(readback.p,0);return bytes;
    }
    static CompositionFixture* active;
    static void create(void*,std::uint32_t,const DirectMode::SwapTextureSetDesc_t* requested,DirectMode::SwapTextureSet_t* output){
        auto& self=*active;check(self.created<self.sources.size(),"bounded actual source sets");const auto eye=self.created++;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=requested->nWidth;desc.Height=requested->nHeight;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        std::vector<std::uint8_t> bytes(std::size_t(desc.Width)*desc.Height*4);
        for(std::size_t i=0;i<bytes.size();i+=4){bytes[i]=17;bytes[i+1]=29;bytes[i+2]=43;bytes[i+3]=101;}
        D3D11_SUBRESOURCE_DATA initial{bytes.data(),desc.Width*4,0};
        for(unsigned i=0;i<3;++i){check(SUCCEEDED(self.device->CreateTexture2D(&desc,&initial,self.sources[eye][i].out())),"real shared eye allocation");
            Com<IDXGIResource> allocation;check(SUCCEEDED(self.sources[eye][i]->QueryInterface(IID_IDXGIResource,reinterpret_cast<void**>(allocation.out()))),"actual shared allocation identity");
            HANDLE handle=nullptr;check(SUCCEEDED(allocation->GetSharedHandle(&handle))&&handle,"real legacy shared handle");output->rSharedTextureHandles[i]=reinterpret_cast<std::uintptr_t>(handle);
        }
        output->unTextureFlags=0;
        check(SUCCEEDED(self.device->CreateShaderResourceView(self.sources[eye][0].p,nullptr,self.eyes[eye].out())),"actual per-eye native SRV");
    }
    static void destroy(void*,vr::SharedTextureHandle_t){}
    static void destroyAll(void*,std::uint32_t){}
    static void next(void*,vr::SharedTextureHandle_t[2],std::uint32_t(*)[2]){}
    static void submit(void*,const DirectMode::SubmitLayerPerEye_t(&)[2]){}
    static void present(void*,vr::SharedTextureHandle_t){active->render();}
    static void post(void*,const DirectMode::Throttling_t*){}
    static void timing(void*,vr::DriverDirectMode_FrameTiming*){}
    void target(Com<ID3D11Texture2D>& texture,Com<ID3D11RenderTargetView>* view,UINT width,UINT height,DXGI_FORMAT format){
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=format;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        check(SUCCEEDED(device->CreateTexture2D(&d,nullptr,texture.out())),"native production graph texture");
        if(view)check(SUCCEEDED(device->CreateRenderTargetView(texture.p,nullptr,view->out())),"native production graph view");
    }
    void prepare(){
        const auto chromaFormat=planarFormat==DXGI_FORMAT_P010?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R16G16_FLOAT;
        target(image,&imageTarget,Width,Height,DXGI_FORMAT_R16G16B16A16_FLOAT);target(chroma,&chromaTarget,Width/2,Height/2,chromaFormat);
        target(foreignChroma,nullptr,Width/2,Height/2,chromaFormat);target(planar,nullptr,Width,Height,planarFormat);target(foreignPlanar,nullptr,Width,Height,planarFormat);
        target(input,nullptr,Width,Height,planarFormat);target(unrelated,&unrelatedTarget,Width,Height,DXGI_FORMAT_R16G16B16A16_FLOAT);
        check(SUCCEEDED(device->CreateShaderResourceView(image.p,nullptr,imageView.out()))&&SUCCEEDED(device->CreateShaderResourceView(chroma.p,nullptr,chromaView.out()))&&SUCCEEDED(device->CreateShaderResourceView(foreignChroma.p,nullptr,foreignChromaView.out())),"native graph source views");
        D3D11_RENDER_TARGET_VIEW_DESC view{};view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;view.Format=planarFormat==DXGI_FORMAT_P010?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM;
        check(SUCCEEDED(device->CreateRenderTargetView(planar.p,&view,yTarget.out()))&&SUCCEEDED(device->CreateRenderTargetView(foreignPlanar.p,&view,foreignY.out())),"actual native planar luma targets");
        view.Format=planarFormat==DXGI_FORMAT_P010?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM;check(SUCCEEDED(device->CreateRenderTargetView(planar.p,&view,uvTarget.out()))&&SUCCEEDED(device->CreateRenderTargetView(foreignPlanar.p,&view,foreignUV.out())),"actual native planar UV targets");
        D3D11_TEXTURE2D_DESC staging{};planar->GetDesc(&staging);staging.Usage=D3D11_USAGE_STAGING;staging.BindFlags=0;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(SUCCEEDED(device->CreateTexture2D(&staging,nullptr,readback.out())),"owned native planar comparison surface");
        D3D11_BUFFER_DESC b{};b.ByteWidth=48;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(SUCCEEDED(device->CreateBuffer(&b,nullptr,constants.out())),"native b2 copy parameters");
        D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;check(SUCCEEDED(device->CreateRasterizerState(&r,raster.out())),"native copy rasterizer");
    }
    void render(){
        // Compare every actual output plane with the same native drawing
        // through the current original dispatch. This is an owned WARP test,
        // not live-process image capture or hardware encoder acceptance.
        renderCore();
        if(mode==Mode::Valid||mode==Mode::UnrelatedWrite||mode==Mode::DirectUV||mode==Mode::DirectUVLumaFirst){
            const unsigned which=mode==Mode::DirectUV?1u:mode==Mode::DirectUVLumaFirst?2u:0u;
            check(nativeReferences[which]==output(),"production coverage leaves every native planar RGB byte unchanged");
        }
    }
    void prepareReferences(){
        // Produce the comparison images before the production facade exists.
        // A second render in an admitted Present would legitimately revoke
        // that presentation's already completed resource graph.
        bypass=true;eyeBase=2;
        const Mode modes[]{Mode::Valid,Mode::DirectUV,Mode::DirectUVLumaFirst};
        for(unsigned i=0;i<3;++i){
            nativeTable=*reinterpret_cast<void***>(context);mode=modes[i];renderCore();
            nativeTable=*reinterpret_cast<void***>(context);nativeReferences[i]=output();
        }
        mode=Mode::Valid;eyeBase=0;bypass=false;
    }
    void renderCore(){
        std::puts("Owned production render: enter");std::fflush(stdout);
        check(bypass||DirectModeBoundary::currentFrame()!=nullptr,"real typed DirectMode presentation scope or explicitly unobserved reference");
        context->ClearState();context->RSSetState(raster.p);context->VSSetShader(vertex.p,nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->PSSetShader(copyPixel.p,nullptr,0);context->PSSetConstantBuffers(2,1,&constants.p);context->OMSetRenderTargets(1,&imageTarget.p,nullptr);
        std::puts("Owned production render: bindings ready");std::fflush(stdout);
        for(unsigned tile=0;tile<4;++tile){if(tile==3&&mode==Mode::MissingTile)continue;const unsigned eye=mode==Mode::WrongEye?0:tile/2;
            const D3D11_VIEWPORT viewport{0,float(tile*Height/4),float(Width),float(Height/4),0,1};context->RSSetViewports(1,&viewport);
            float b[12]{};b[9]=float((tile/2)*Height/2);context->UpdateSubresource(constants.p,0,nullptr,b,0,0);context->PSSetShaderResources(0,1,&eyes[eyeBase+eye].p);
            std::printf("Owned production render: draw tile %u\n",tile);std::fflush(stdout);draw();
        }
        const FLOAT zero[4]{};
        if(mode==Mode::ClearImage)context->ClearRenderTargetView(imageTarget.p,zero);
        if(mode==Mode::UnrelatedWrite)context->ClearRenderTargetView(unrelatedTarget.p,zero);
        if(mode==Mode::RegionWrite)context->CopySubresourceRegion(image.p,0,0,0,0,unrelated.p,0,nullptr);
        if(mode==Mode::DiscardImage){Com<ID3D11DeviceContext1> extended;check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(extended.out()))),"native Context1 discard");extended->DiscardView(imageTarget.p);}
        if(mode==Mode::UnknownDraw){context->PSSetShader(unknownPixel.p,nullptr,0);draw();}
        ID3D11ShaderResourceView* empty[2]{};context->PSSetShaderResources(0,2,empty);
        if(mode>=Mode::DirectUV){
            const auto luma=[&]{context->OMSetRenderTargets(1,&yTarget.p,nullptr);context->PSSetShaderResources(0,1,&imageView.p);context->PSSetShader(lumaPixel.p,nullptr,0);
                const D3D11_VIEWPORT view{0,0,float(Width),float(Height),0,1};context->RSSetViewports(1,&view);draw();};
            if(mode==Mode::DirectUVLumaFirst)luma();
            auto* uv=mode==Mode::DirectUVWrongOutput?foreignUV.p:uvTarget.p;auto* source=mode==Mode::DirectUVWrongSource?foreignChromaView.p:imageView.p;
            context->OMSetRenderTargets(1,&uv,nullptr);context->PSSetShaderResources(0,1,&source);context->PSSetShader(chromaPixel.p,nullptr,0);
            const D3D11_VIEWPORT view{0,0,float(Width/2),float(Height/2),0,1};context->RSSetViewports(1,&view);draw();
            if(mode==Mode::DirectUVRepeated)draw();
            if(mode==Mode::DirectUVMixed){context->OMSetRenderTargets(1,&chromaTarget.p,nullptr);context->PSSetShaderResources(0,1,&imageView.p);draw();}
            if(mode!=Mode::DirectUVLumaFirst)luma();
            copy(input.p,planar.p);return;
        }
        context->OMSetRenderTargets(1,&chromaTarget.p,nullptr);context->PSSetShaderResources(0,1,&imageView.p);context->PSSetShader(chromaPixel.p,nullptr,0);
        D3D11_VIEWPORT viewport{0,0,float(Width/2),float(Height/2),0,1};context->RSSetViewports(1,&viewport);draw();
        context->OMSetRenderTargets(1,&uvTarget.p,nullptr);auto* uvSource=mode==Mode::WrongChroma?foreignChromaView.p:chromaView.p;context->PSSetShaderResources(1,1,&uvSource);context->PSSetShader(packPixel.p,nullptr,0);draw();
        auto* y=mode==Mode::WrongLuma?foreignY.p:yTarget.p;context->OMSetRenderTargets(1,&y,nullptr);context->PSSetShader(lumaPixel.p,nullptr,0);viewport.Width=float(Width);viewport.Height=float(Height);context->RSSetViewports(1,&viewport);draw();
        copy(input.p,planar.p);
    }
};
CompositionFixture* CompositionFixture::active=nullptr;
NVENCSTATUS NVENCAPI compositionRegister(void*,NV_ENC_REGISTER_RESOURCE* resource){resource->registeredResource=reinterpret_cast<NV_ENC_REGISTERED_PTR>(0x2020);return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI compositionMap(void*,NV_ENC_MAP_INPUT_RESOURCE* input){input->mappedResource=reinterpret_cast<NV_ENC_INPUT_PTR>(0x3030);input->mappedBufferFmt=CompositionFixture::active->encoderFormat();return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI compositionUnmap(void*,NV_ENC_INPUT_PTR){return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI compositionUnregister(void*,NV_ENC_REGISTERED_PTR){return NV_ENC_SUCCESS;}
std::uint64_t compositionTimestamp=0;NV_ENC_OUTPUT_PTR compositionOutput=nullptr;
NVENCSTATUS NVENCAPI compositionEncode(void*,NV_ENC_PIC_PARAMS* picture){compositionTimestamp=picture->inputTimeStamp;compositionOutput=picture->outputBitstream;return NV_ENC_SUCCESS;}
NVENCSTATUS NVENCAPI compositionLock(void*,NV_ENC_LOCK_BITSTREAM* packet){
    // Explicitly synthetic completion ports; no hardware encoding or peer.
    static std::uint8_t ownedBytes[]{0,0,0,1,2,1,0x80};
    if(packet->outputBitstream!=compositionOutput)return NV_ENC_ERR_INVALID_PARAM;
    packet->bitstreamBufferPtr=ownedBytes;packet->bitstreamSizeInBytes=sizeof ownedBytes;packet->outputTimeStamp=compositionTimestamp;
    packet->pictureType=NV_ENC_PIC_TYPE_P;packet->pictureStruct=NV_ENC_PIC_STRUCT_FRAME;return NV_ENC_SUCCESS;
}
NVENCSTATUS NVENCAPI compositionFactory(NV_ENCODE_API_FUNCTION_LIST* api){factory(api);api->nvEncRegisterResource=compositionRegister;api->nvEncMapInputResource=compositionMap;api->nvEncUnmapInputResource=compositionUnmap;api->nvEncUnregisterResource=compositionUnregister;api->nvEncEncodePicture=compositionEncode;api->nvEncLockBitstream=compositionLock;return NV_ENC_SUCCESS;}
void runComposition(DXGI_FORMAT planarFormat=DXGI_FORMAT_NV12,bool legacy=false){
    const auto mark=[](const char* stage){std::printf("Owned production composition: %s\n",stage);std::fflush(stdout);};
    mark("compile owned shader contracts");
    static constexpr char shader[]=R"(
cbuffer Parameters:register(b2){float4 unused0;float4 unused1;float4 offset;};
Texture2D<float4> scene:register(t0);Texture2D<float2> uv:register(t1);
float4 vs(uint id:SV_VertexID):SV_Position{float2 p=float2((id<<1)&2,id&2);return float4(p*float2(2,-2)+float2(-1,1),0,1);}
float4 copy(float4 p:SV_Position):SV_Target{return float4(scene.Load(int3(int2(p.xy-offset.xy),0)).rgb,1);}
float4 chroma(float4 p:SV_Position):SV_Target{return float4(scene.Load(int3(int2(p.xy)*2,0)).rg,0,1);}
float4 pack(float4 p:SV_Position):SV_Target{return float4(uv.Load(int3(int2(p.xy),0)),0,1);}
float4 luma(float4 p:SV_Position):SV_Target{return float4(scene.Load(int3(int2(p.xy),0)).r,0,0,1);}
float4 other():SV_Target{return float4(0,0,0,1);}
)";
    const char* entries[]{"vs","copy","chroma","pack","luma","other"};
    const NativeShader kinds[]{NativeShader::FullscreenVertex,NativeShader::CopyView,legacy?NativeShader::ChromaLegacy:NativeShader::Chroma,NativeShader::ChromaPack,legacy?NativeShader::LumaLegacy:NativeShader::Luma};
    std::array<Com<ID3DBlob>,6> code;std::array<ShaderFingerprint,5> pins{};
    for(unsigned i=0;i<code.size();++i){check(SUCCEEDED(D3DCompile(shader,sizeof shader-1,"sfnp-owned-production-graph",nullptr,nullptr,entries[i],i?"ps_5_0":"vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,code[i].out(),nullptr)),"owned production graph shader");if(i<pins.size()){pins[i].kind=kinds[i];check(dxbcFingerprint(code[i]->GetBufferPointer(),code[i]->GetBufferSize(),pins[i].hash),"owned actual shader signature contract");}}
    mark("construct production observer");
    NvencBoundary boundary({{1}});DirectModeBoundary direct;ControlRuntime control;SenderCoverage coverage(direct,boundary,control,pins.data(),pins.size());
    NativeImageLayout layout;const auto module=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));layout.imageSize=16*1024*1024;layout.executableCount=1;layout.executable[0]={0x1000,layout.imageSize-0x1000};
    HMODULE runtime=LoadLibraryW(L"d3d11.dll");check(runtime!=nullptr,"public native device runtime");static void* factorySlot=nullptr;factorySlot=pointerBits<void*>(GetProcAddress(runtime,"D3D11CreateDevice"));
    check(reinterpret_cast<std::uintptr_t>(&factorySlot)>module&&reinterpret_cast<std::uintptr_t>(&factorySlot)-module<layout.imageSize,"owned fixture import is inside the synthetic verified image");layout.createDevice=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&factorySlot)-module);
    check(coverage.configure(module,layout),"actual production shader observer armed before owned native factory");mark("create native device");
    Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    check(SUCCEEDED(pointerBits<decltype(&D3D11CreateDevice)>(factorySlot)(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.out(),&level,context.out())),"native WARP device through production factory observer");
    Com<ID3D11Multithread> multithread;check(SUCCEEDED(context->QueryInterface(__uuidof(ID3D11Multithread),reinterpret_cast<void**>(multithread.out()))),"actual native multithread interface");multithread->SetMultithreadProtected(TRUE);
    mark("create actual native shader objects");
    CompositionFixture fixture;fixture.planarFormat=planarFormat;fixture.device=device.p;fixture.context=context.p;fixture.nativeTable=*reinterpret_cast<void***>(context.p);CompositionFixture::active=&fixture;
    check(SUCCEEDED(device->CreateVertexShader(code[0]->GetBufferPointer(),code[0]->GetBufferSize(),nullptr,fixture.vertex.out())),"production recognized native vertex shader");
    ID3D11PixelShader** pixels[]{fixture.copyPixel.out(),fixture.chromaPixel.out(),fixture.packPixel.out(),fixture.lumaPixel.out(),fixture.unknownPixel.out()};
    for(unsigned i=0;i<5;++i)check(SUCCEEDED(device->CreatePixelShader(code[i+1]->GetBufferPointer(),code[i+1]->GetBufferSize(),nullptr,pixels[i])),"production observed actual native pixel creation");
    mark("create native image graph");fixture.prepare();check(direct.prepare({CompositionFixture::create,CompositionFixture::destroy,CompositionFixture::destroyAll,CompositionFixture::next,CompositionFixture::submit,CompositionFixture::present,CompositionFixture::post,CompositionFixture::timing}),"typed DirectMode public callbacks");
    auto calls=direct.entryPoints();DirectMode::SwapTextureSetDesc_t desc{CompositionFixture::Width,CompositionFixture::Height/2,unsigned(DXGI_FORMAT_R8G8B8A8_UNORM),9};
    DirectMode::SwapTextureSet_t sets[4]{};const auto pid=GetCurrentProcessId();const auto compositorPid=pid==1?2u:1u;
    for(unsigned eye=0;eye<4;++eye)calls.create(&fixture,eye<2?compositorPid:pid,&desc,&sets[eye]);
    mark("open explicit fake NVENC ports");
    NV_ENCODE_API_FUNCTION_LIST api{};api.version=NV_ENCODE_API_FUNCTION_LIST_VER;check(boundary.create(compositionFactory,&api)==NV_ENC_SUCCESS,"production NVENC ports installed");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS parameters{};parameters.version=NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;parameters.device=device.p;parameters.deviceType=NV_ENC_DEVICE_TYPE_DIRECTX;void* encoder=nullptr;check(api.nvEncOpenEncodeSessionEx(&parameters,&encoder)==NV_ENC_SUCCESS,"production observes actual encoder device");
    NV_ENC_INITIALIZE_PARAMS init{};init.version=NV_ENC_INITIALIZE_PARAMS_VER;init.encodeGUID=NV_ENC_CODEC_HEVC_GUID;init.encodeWidth=init.darWidth=CompositionFixture::Width;init.encodeHeight=init.darHeight=CompositionFixture::Height;init.frameRateNum=90;init.frameRateDen=1;
    check(api.nvEncInitializeEncoder(encoder,&init)==NV_ENC_SUCCESS,"owned HEVC encoder contract");fixture.prepareReferences();mark("prepare production context and coverage pool");coverage.tick();
    check(coverage.gateStats().contextsReady==1&&coverage.readiness().prepared,"production pool and native context ready");
    NV_ENC_REGISTER_RESOURCE registered{};registered.version=NV_ENC_REGISTER_RESOURCE_VER;registered.resourceType=NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;registered.resourceToRegister=fixture.input.p;registered.width=CompositionFixture::Width;registered.height=CompositionFixture::Height;registered.bufferFormat=fixture.encoderFormat();
    check(api.nvEncRegisterResource(encoder,&registered)==NV_ENC_SUCCESS,"actual encoder input resource registered");
    DirectMode::SubmitLayerPerEye_t eyes[2]{};for(unsigned eye=0;eye<2;++eye){eyes[eye].hTexture=sets[eye].rSharedTextureHandles[0];eyes[eye].bounds={0,0,1,1};eyes[eye].mProjection.m[0][0]=eyes[eye].mProjection.m[1][1]=1;eyes[eye].mHmdPose.m[0][0]=eyes[eye].mHmdPose.m[1][1]=eyes[eye].mHmdPose.m[2][2]=1;}
    calls.submit(&fixture,eyes);calls.present(&fixture,0);
    check(coverage.readiness().preparedPid==compositorPid&&!coverage.ready(pid)&&!coverage.operational(pid),"compositor startup images do not establish an exact application source or operational coverage");
    check(coverage.formatReady(pid)&&!coverage.formatReady(0)&&!coverage.formatReady(pid+16),"registered application may negotiate source format before its first RGBA frame; unknown and zero PIDs remain refused");
    SourceIpcRequest app{};app.processId=pid;app.nonce[0]=9;SourceIpcPermission permission;SourceIpcState peer{};peer.binding[0]=7;peer.generation=4;
    permission.update(app,pid,peer,coverage.formatReady(pid));check(!permission.process(),"prepared source format without a compatible peer cannot grant permission");
    peer.compatible=true;const auto answer=permission.update(app,pid,peer,coverage.formatReady(pid));
    check(permission.process()==pid&&answer[10]==1&&!coverage.ready(pid),"format-only reply bootstraps the actual app before its native source is ready and never claims operational coverage");
    coverage.sourceProcess(permission.process());
    const auto foreignBefore=coverage.gateStats();calls.submit(&fixture,eyes);calls.present(&fixture,0);const auto foreignAfter=coverage.gateStats();
    check(foreignAfter.completePairs==foreignBefore.completePairs+1&&foreignAfter.capturedCopies==foreignBefore.capturedCopies&&!foreignAfter.certifiedMasks&&!coverage.operational(pid),"a complete compositor image under a live application format lease never becomes an application mask");
    fixture.eyeBase=2;for(unsigned eye=0;eye<2;++eye)eyes[eye].hTexture=sets[2+eye].rSharedTextureHandles[0];
    std::uint64_t timestamp=0;
    for(auto mode:{CompositionFixture::Mode::Valid,CompositionFixture::Mode::ClearImage,CompositionFixture::Mode::MissingTile,CompositionFixture::Mode::WrongEye,CompositionFixture::Mode::WrongChroma,CompositionFixture::Mode::WrongLuma,CompositionFixture::Mode::UnknownDraw,CompositionFixture::Mode::RegionWrite,CompositionFixture::Mode::DiscardImage,CompositionFixture::Mode::UnrelatedWrite,CompositionFixture::Mode::DirectUV,CompositionFixture::Mode::DirectUVLumaFirst,CompositionFixture::Mode::DirectUVWrongSource,CompositionFixture::Mode::DirectUVWrongOutput,CompositionFixture::Mode::DirectUVRepeated,CompositionFixture::Mode::DirectUVMixed,CompositionFixture::Mode::Valid}){
        std::printf("Owned production composition: present mode %u\n",unsigned(mode));std::fflush(stdout);
        fixture.mode=mode;const bool valid=mode==CompositionFixture::Mode::Valid||mode==CompositionFixture::Mode::UnrelatedWrite||mode==CompositionFixture::Mode::DirectUV||mode==CompositionFixture::Mode::DirectUVLumaFirst;const auto before=coverage.gateStats();calls.submit(&fixture,eyes);calls.present(&fixture,0);
        NV_ENC_MAP_INPUT_RESOURCE mapped{};mapped.version=NV_ENC_MAP_INPUT_RESOURCE_VER;mapped.registeredResource=registered.registeredResource;check(api.nvEncMapInputResource(encoder,&mapped)==NV_ENC_SUCCESS,"native input mapping");
        NV_ENC_PIC_PARAMS picture{};picture.version=NV_ENC_PIC_PARAMS_VER;picture.inputBuffer=mapped.mappedResource;picture.bufferFmt=fixture.encoderFormat();picture.inputWidth=CompositionFixture::Width;picture.inputHeight=CompositionFixture::Height;picture.inputTimeStamp=++timestamp;picture.outputBitstream=reinterpret_cast<NV_ENC_OUTPUT_PTR>(0x4040+timestamp*16);picture.pictureStruct=NV_ENC_PIC_STRUCT_FRAME;
        check(api.nvEncEncodePicture(encoder,&picture)==NV_ENC_SUCCESS,"native encode request forwarded unchanged");const auto after=coverage.gateStats();
        if(after.capturedCopies!=before.capturedCopies+unsigned(valid)||after.matchedInputs!=before.matchedInputs+unsigned(valid)){
            std::printf("composition mode=%u captures=%llu/%llu inputs=%llu/%llu last=%s first-step=%s\n",unsigned(mode),static_cast<unsigned long long>(before.capturedCopies),static_cast<unsigned long long>(after.capturedCopies),static_cast<unsigned long long>(before.matchedInputs),static_cast<unsigned long long>(after.matchedInputs),coverageGateName(after.lastRefusal),coverageGateName(static_cast<CoverageGate>(after.compositionRefusal[13])));
            for(unsigned i=1;i<after.refusals.size();++i)if(after.refusals[i]!=before.refusals[i])std::printf("composition refusal %s: +%llu\n",coverageGateName(static_cast<CoverageGate>(i)),static_cast<unsigned long long>(after.refusals[i]-before.refusals[i]));
        }
        check(after.capturedCopies==before.capturedCopies+unsigned(valid)&&after.matchedInputs==before.matchedInputs+unsigned(valid),"only the complete unchanged current source graph reaches its exact accepted native encoder input");
        check(!after.certifiedMasks,"synthetic NVENC acceptance without a compatible peer cannot fabricate a mask certificate");
        NV_ENC_LOCK_BITSTREAM completed{};completed.version=NV_ENC_LOCK_BITSTREAM_VER;completed.outputBitstream=picture.outputBitstream;
        check(api.nvEncLockBitstream(encoder,&completed)==NV_ENC_SUCCESS&&completed.outputTimeStamp==picture.inputTimeStamp,"owned fake completion addresses the exact submitted input");
        check(api.nvEncUnlockBitstream(encoder,picture.outputBitstream)==NV_ENC_SUCCESS,"owned fake output release retires its exact coverage stage");
        check(!coverage.gateStats().certifiedMasks,"fake completion without a compatible peer cannot grant a real mask certificate");
        check(api.nvEncUnmapInputResource(encoder,mapped.mappedResource)==NV_ENC_SUCCESS,"exact input retirement");
    }
    for(unsigned eye=0;eye<2;++eye){SourceAlphaSample alpha{};
        check(coverage.tileAlpha(eye,alpha)&&alpha.source.origin==1&&alpha.source.declaredPid==pid&&
            (alpha.source.pid==pid||alpha.source.pid==compositorPid)&&alpha.source.sourceBit==(1u<<eye)&&alpha.source.present,
            "actual staged tile statistics retain their real producer separately from the declared app and grant no source substitution");
        for(auto value:alpha.alpha)check((value&255)==101&&((value>>8)&255)==101&&((value>>16)&255)==101,
            "native staged tile alpha statistics preserve the actual fractional source value");
    }
    permission.reset();coverage.sourceProcess(0);const auto revoked=coverage.gateStats();calls.submit(&fixture,eyes);calls.present(&fixture,0);
    check(coverage.gateStats().capturedCopies==revoked.capturedCopies&&!coverage.gateStats().certifiedMasks,"source withdrawal refuses a new exact app image without borrowing its previous format lease");
    calls.destroyAll(&fixture,pid);check(!coverage.formatReady(pid),"removing the application's registered swaps prevents new format admission");
    check(coverage.gateStats().nativeShadersRecognized==5,"all actual production shader identities were observed at native creation");coverage.stop();check(!coverage.formatReady(compositorPid),"stopped coverage cannot grant source format");direct.stop();
}
