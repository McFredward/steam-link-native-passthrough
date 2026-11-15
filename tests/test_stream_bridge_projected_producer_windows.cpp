#include "pc/stream-bridge-projected-alpha-producer.h"
#include "pc/stream-bridge-projected-alpha-consumer.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace stream_bridge::sender;
using namespace stream_bridge::sender::projected_alpha;
namespace {
unsigned checks=0;Producer* active=nullptr;
struct CrossProcess {ChannelLaunch launch{};std::uint64_t source=0,native=0;std::uint32_t compositorPid=0;};
void check(bool okay,const char* why){++checks;if(!okay)throw std::runtime_error(why);}
void releasing(const MutexLease& lease,std::uint64_t key,void*)noexcept{if(active)active->releasing(lease,key);}
void released(const MutexLease& lease,HRESULT result,void*)noexcept{if(active)active->released(lease,result);}
void STDMETHODCALLTYPE draw(ID3D11DeviceContext* context,UINT count,UINT start){context->Draw(count,start);}
constexpr char ProducerFixture[]=R"(
cbuffer Scene : register(b0) {float4 words[4];};
Texture2D<float4> scene : register(t0);
RWTexture2D<uint> scratch : register(u0);
RWByteAddressBuffer positions : register(u1);
[numthreads(1,1,1)] void compute(uint3 id:SV_DispatchThreadID){scratch[id.xy]=123;}
[numthreads(1,1,1)] void outside(){
    positions.Store4(0,asuint(float4(4,4,7,7)));positions.Store4(16,asuint(float4(4,5,7,8)));
    positions.Store4(32,asuint(float4(5,4,8,7)));positions.Store4(48,asuint(float4(5,5,8,8)));
}
float4 vs(float2 position:POSITION,float2 uv:TEXCOORD0):SV_Position{return float4(2*position-1,0,1);}
float4 ps(float4 xy:SV_Position):SV_Target{
    float4 value=scene.Load(int3(int2(xy.xy),0));
    value.rgb*=words[1].xyz;
    value.a=words[3].y==0?1:value.a;
    return value;
}
)";
Reference<ID3DBlob> compile(const char* entry,const char* target){
    Reference<ID3DBlob> code,error;const auto result=D3DCompile(ProducerFixture,sizeof ProducerFixture-1,nullptr,nullptr,nullptr,
        entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,code.out(),error.out());
    if(error.value)std::fwrite(error->GetBufferPointer(),1,error->GetBufferSize(),stderr);
    check(SUCCEEDED(result),"owned producer shader compilation");return code;
}
std::vector<std::uint8_t> pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* image){
    D3D11_TEXTURE2D_DESC desc{};image->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Reference<ID3D11Texture2D> read;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,read.out())),"owned readback allocation");
    context->CopyResource(read.value,image);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(read.value,0,D3D11_MAP_READ,0,&mapped)),"owned pixels map");
    std::vector<std::uint8_t> output(desc.Width*desc.Height*4);
    for(UINT y=0;y<desc.Height;++y)std::memcpy(output.data()+y*desc.Width*4,static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch,desc.Width*4);
    context->Unmap(read.value,0);return output;
}
std::uint64_t handle(ID3D11Texture2D* image){Reference<IDXGIResource> resource;HANDLE result=nullptr;
    check(SUCCEEDED(image->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(resource.out())))&&SUCCEEDED(resource->GetSharedHandle(&result))&&result,"owned shared allocation identity");
    return std::uint64_t(reinterpret_cast<std::uintptr_t>(result));
}
void run(const CrossProcess* incoming=nullptr,ChannelPage* external=nullptr){
    auto vsCode=compile("vs","vs_5_0"),psCode=compile("ps","ps_5_0");
    Reference<ID3D11ShaderReflection> reflection;D3D11_SHADER_DESC signature{};
    check(SUCCEEDED(D3DReflect(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(reflection.out())))&&SUCCEEDED(reflection->GetDesc(&signature))&&signature.InputParameters==2,
        "actual owned vertex shader retains the native two-attribute input signature");
    std::array<ShaderFingerprint,2> contracts{};contracts[0].kind=NativeShader::StreamVertex;contracts[1].kind=NativeShader::MultiViewInner;
    check(dxbcFingerprint(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),contracts[0].hash)&&
        dxbcFingerprint(psCode->GetBufferPointer(),psCode->GetBufferSize(),contracts[1].hash),"owned shader contracts");
    NativeShaders shaders(contracts.data(),contracts.size(),nullptr,nullptr,true);
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_SINGLETHREADED,nullptr,0,
        D3D11_SDK_VERSION,device.out(),nullptr,context.out()))&&shaders.observeDevice(device.value),"owned actual native-style device");
    MutexObserver locks({nullptr,releasing,released,nullptr});check(locks.prepare(device.value),"original native synchronization observed");
    ChannelParent channel;check(channel.create(),"private producer channel");
    auto* page=external?external:channel.page();
    const unsigned policyBytes=64;Producer producer(&contracts[1],&policyBytes,1);active=&producer;
    check(producer.prepare(device.value,page,shaders,locks),"production controller prepares embedded own kernels");
    constexpr UINT width=40,height=36;const std::uint32_t scenePid=77,compositorPid=incoming?incoming->compositorPid:88;
    const std::uint64_t lease=(std::uint64_t(7)<<32)|scenePid;channel.sourceLease(lease);
    std::vector<std::uint8_t> input(width*height*4);
    for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17)%256;input[i*4+1]=UINT(i*39)%256;input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;D3D11_SUBRESOURCE_DATA initial{input.data(),width*4,0};
    Reference<ID3D11Texture2D> source,native;Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11RenderTargetView> target;
    const auto open=[&](std::uint64_t value,ID3D11Texture2D** output){return device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(value)),__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(output));};
    check((incoming?(SUCCEEDED(open(incoming->source,source.out()))&&SUCCEEDED(open(incoming->native,native.out()))):
        (SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out()))))&&
        SUCCEEDED(device->CreateShaderResourceView(source.value,nullptr,sourceView.out()))&&SUCCEEDED(device->CreateRenderTargetView(native.value,nullptr,target.out())),"actual owned source and native target");
    producer.target(target.value);const auto sourceHandle=handle(source.value),nativeHandle=handle(native.value);
    MaskRegistration prepared{};check(page->registrations[0].read(prepared)&&prepared.maskHandle&&producer.stage()==2,"cold preparation published the actual companion allocation");
    Registry registry{};registry.sourceLease=lease;registry.revision=1;
    registry.retained[0]={sourceHandle,21,scenePid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    registry.retained[1]={nativeHandle,22,compositorPid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=source.value;pins[1]=native.value;
    check(external||channel.publishRetained(registry,pins),"real held source registry publication");
    Reference<ID3D11VertexShader> vs;Reference<ID3D11PixelShader> ps;Reference<ID3D11InputLayout> layout;Reference<ID3D11Buffer> vertices,constants;
    check(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(),vsCode->GetBufferSize(),nullptr,vs.out()))&&
        SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(),psCode->GetBufferSize(),nullptr,ps.out())),"actual observed native-model program objects");
    check(shaders.identify(vs.value)==NativeShader::StreamVertex&&shaders.identify(ps.value)==NativeShader::MultiViewInner,"actual shader observations survive cold companion preparation");
    const D3D11_INPUT_ELEMENT_DESC elements[]{
        {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0}};
    check(SUCCEEDED(device->CreateInputLayout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.out())),"actual native-style position plus UV layout");
    producer.geometry().layout(elements,2,vsCode->GetBufferPointer(),vsCode->GetBufferSize(),layout.value);
    // Deliberately different UVs must not become position evidence.
    const std::array<float,16> points{{0,0,7,7,0,1,7,8,1,0,8,7,1,1,8,8}};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof points;buffer.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vertexData{points.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&vertexData,vertices.out())),"actual vertex buffer");
    producer.geometry().buffer(&buffer,&vertexData,vertices.value);
    std::array<float,16> words{};words[4]=words[5]=words[6]=1;buffer.ByteWidth=sizeof words;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA constantData{words.data(),0,0};check(SUCCEEDED(device->CreateBuffer(&buffer,&constantData,constants.out())),"native alpha policy constant buffer");
    Reference<IDXGIKeyedMutex> sourceMutex,streamMutex;
    check(SUCCEEDED(source->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(sourceMutex.out())))&&
        SUCCEEDED(native->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(streamMutex.out()))),"original application and stream mutexes");
    check(sourceMutex->AcquireSync(0,0)==S_OK&&streamMutex->AcquireSync(0,0)==S_OK,"actual producer ownership");
    context->VSSetShader(vs.value,nullptr,0);context->PSSetShader(ps.value,nullptr,0);context->PSSetConstantBuffers(0,1,&constants.value);
    context->IASetInputLayout(layout.value);UINT stride=16,offset=0;context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->OMSetRenderTargets(1,&target.value,nullptr);context->PSSetShaderResources(0,1,&sourceView.value);
    const D3D11_VIEWPORT viewport{0,0,float(width),float(height),0,1};context->RSSetViewports(1,&viewport);
    Reference<ID3D11VertexShader> boundVertex;Reference<ID3D11RenderTargetView> boundTarget;
    context->VSGetShader(boundVertex.out(),nullptr,nullptr);context->OMGetRenderTargets(1,boundTarget.out(),nullptr);
    check(boundTarget.value==target.value&&shaders.identify(boundVertex.value)==NativeShader::StreamVertex,"actual native target and vertex program are bound under ownership");
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"production bound vertex range proves full image coverage");
    check(bool(locks.current(device.value,sourceHandle)),"actual source-allocation ownership is retained");
    auto computeCode=compile("compute","cs_5_0"),outsideCode=compile("outside","cs_5_0");
    Reference<ID3D11ComputeShader> compute,outside;
    check(SUCCEEDED(device->CreateComputeShader(computeCode->GetBufferPointer(),computeCode->GetBufferSize(),nullptr,compute.out()))&&
        SUCCEEDED(device->CreateComputeShader(outsideCode->GetBufferPointer(),outsideCode->GetBufferSize(),nullptr,outside.out())),"actual native-style independent compute passes");
    D3D11_TEXTURE2D_DESC scratchDesc{};scratchDesc.Width=scratchDesc.Height=scratchDesc.MipLevels=scratchDesc.ArraySize=scratchDesc.SampleDesc.Count=1;
    scratchDesc.Format=DXGI_FORMAT_R32_UINT;scratchDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    Reference<ID3D11Texture2D> scratchImage;Reference<ID3D11UnorderedAccessView> scratchView;
    check(SUCCEEDED(device->CreateTexture2D(&scratchDesc,nullptr,scratchImage.out()))&&
        SUCCEEDED(device->CreateUnorderedAccessView(scratchImage.value,nullptr,scratchView.out())),"separate writable compute output");
    context->CSSetShader(compute.value,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&scratchView.value,nullptr);
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"unrelated compute cannot erase actual native vertex creation evidence");
    D3D11_BUFFER_DESC mutableDesc{};mutableDesc.ByteWidth=sizeof points;mutableDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_UNORDERED_ACCESS;
    mutableDesc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    Reference<ID3D11Buffer> mutableVertices;Reference<ID3D11UnorderedAccessView> mutableView;
    check(SUCCEEDED(device->CreateBuffer(&mutableDesc,&vertexData,mutableVertices.out())),"native vertex allocation with explicit raw compute access");
    D3D11_UNORDERED_ACCESS_VIEW_DESC mutableViewDesc{};mutableViewDesc.Format=DXGI_FORMAT_R32_TYPELESS;
    mutableViewDesc.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;mutableViewDesc.Buffer.NumElements=sizeof points/4;mutableViewDesc.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
    check(SUCCEEDED(device->CreateUnorderedAccessView(mutableVertices.value,&mutableViewDesc,mutableView.out())),"explicit raw UAV for the actual vertex allocation");
    producer.geometry().buffer(&mutableDesc,&vertexData,mutableVertices.value);
    context->IASetVertexBuffers(0,1,&mutableVertices.value,&stride,&offset);
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"known mutable vertex data initially covers the actual target");
    ID3D11Buffer* noBuffer=nullptr;context->IASetVertexBuffers(0,1,&noBuffer,&stride,&offset);
    context->CSSetShader(outside.value,nullptr,0);context->CSSetUnorderedAccessViews(1,1,&mutableView.value,nullptr);
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    ID3D11UnorderedAccessView* noView=nullptr;context->CSSetUnorderedAccessViews(1,1,&noView,nullptr);
    context->IASetVertexBuffers(0,1,&mutableVertices.value,&stride,&offset);
    unsigned reason=0;
    check(!producer.geometry().fullUnitQuad(context.value,4,0,width,height,1,&reason)&&reason==7,"actual bound compute writes revoke that vertex allocation before reuse");
    D3D11_BUFFER_DESC readDesc=mutableDesc;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.BindFlags=readDesc.MiscFlags=readDesc.StructureByteStride=0;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Reference<ID3D11Buffer> readVertices;check(SUCCEEDED(device->CreateBuffer(&readDesc,nullptr,readVertices.out())),"owned compute-write reference readback");
    context->CopyResource(readVertices.value,mutableVertices.value);D3D11_MAPPED_SUBRESOURCE mapped{};
    check(SUCCEEDED(context->Map(readVertices.value,0,D3D11_MAP_READ,0,&mapped)),"observe actual changed vertex bytes on the owned GPU target");
    check(static_cast<const float*>(mapped.pData)[0]==4&&static_cast<const float*>(mapped.pData)[1]==4,"compute really moved the vertices outside the image");context->Unmap(readVertices.value,0);
    context->UpdateSubresource(mutableVertices.value,0,nullptr,points.data(),0,0);producer.geometry().update(mutableVertices.value,points.data());
    check(producer.geometry().fullUnitQuad(context.value,4,0,width,height,1),"fresh public vertex data repairs only its actual allocation");
    context->IASetVertexBuffers(0,1,&vertices.value,&stride,&offset);context->CSSetShader(compute.value,nullptr,0);
    producer.draw(context.value,draw,4,0);draw(context.value,4,0);context->Flush();
    producer.dispatch(context.value);context->Dispatch(1,1,1);
    std::printf("production stage after draw=%u\n",producer.stage());
    check(streamMutex->ReleaseSync(3)==S_OK,"native stream ownership transferred with its unchanged release key");
    if(incoming){
        check(sourceMutex->ReleaseSync(0)==S_OK,"child source ownership released after paired image");
        BYTE finished=0;DWORD bytes=0;
        check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&finished,1,&bytes,nullptr)&&bytes==1&&finished==1,"parent acknowledges its actual paired copy");
        active=nullptr;context->ClearState();return;
    }
    std::printf("production stage after release=%u\n",producer.stage());
    ImageWitness witness{};check(page->witnesses[0].read(witness)&&witness.complete&&witness.sourceLease==lease&&
        witness.scene.pid==scenePid&&witness.native.pid==compositorPid&&witness.native.generation==22,"production witness separates authenticated game ancestry from actual compositor ownership");
    bool sealed=false;for(const auto& release:witness.releases)sealed|=release.allocation==nativeHandle&&release.key==3&&release.scope>1;
    check(sealed,"witness sealed by the original native output release");
    Reference<ID3D11Texture2D> mask;check(SUCCEEDED(device->OpenSharedResource(reinterpret_cast<HANDLE>(std::uintptr_t(witness.maskHandle)),
        __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(mask.out()))),"import actual production mask");
    check(streamMutex->AcquireSync(3,0)==S_OK,"actual consumer owns the exact image version");
    const auto nativePixels=pixels(device.value,context.value,native.value),maskPixels=pixels(device.value,context.value,mask.value);
    for(UINT i=0;i<width*height;++i){check(nativePixels[i*4+3]==255,"ordinary native alpha policy remains opaque");
        check(maskPixels[i*4+3]==input[i*4+3],"production mask preserves every zero/fractional/opaque source alpha");
        for(unsigned c=0;c<3;++c)check(nativePixels[i*4+c]==input[i*4+c]&&!maskPixels[i*4+c],"native RGB including additive zero-alpha colour unchanged; no RGB exported");}
    producer.invalidate(native.value);check(page->witnesses[0].read(witness)&&!witness.complete,"unsupported native write revokes the previously sealed image before reuse");
    check(streamMutex->ReleaseSync(0)==S_OK&&streamMutex->AcquireSync(0,0)==S_OK,"next exact native ownership interval");
    channel.sourceLease(lease+0x100000000ull);producer.draw(context.value,draw,4,0);draw(context.value,4,0);
    check(streamMutex->ReleaseSync(0)==S_OK&&page->witnesses[0].read(witness)&&!witness.complete,"revoked app session cannot republish old coverage");
    check(sourceMutex->ReleaseSync(0)==S_OK,"native application ownership ends");
    active=nullptr;context->ClearState();
}
void consumerControl(){
    Consumer consumer;auto& channel=consumer.channel();check(channel.page()!=nullptr,"private parent consumer channel");
    Reference<ID3D11Device> device;Reference<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.out(),nullptr,context.out())),"independent actual parent WARP device");
    constexpr UINT width=40,height=36;std::vector<std::uint8_t> input(width*height*4);
    for(UINT i=0;i<width*height;++i){input[i*4]=UINT(i*17)%256;input[i*4+1]=UINT(i*39)%256;input[i*4+2]=UINT(i*71)%256;input[i*4+3]=i%3==0?0:i%3==1?127:255;}
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    D3D11_SUBRESOURCE_DATA initial{input.data(),width*4,0};Reference<ID3D11Texture2D> source,native;
    check(SUCCEEDED(device->CreateTexture2D(&desc,&initial,source.out()))&&SUCCEEDED(device->CreateTexture2D(&desc,nullptr,native.out())),"parent retains exact application and native output allocations");
    Reference<IDXGIKeyedMutex> sync;
    check(SUCCEEDED(native->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(sync.out()))),"actual parent native synchronization resource");
    struct Handles {HANDLE read=nullptr,write=nullptr,process=nullptr,thread=nullptr;
        ~Handles(){if(write)CloseHandle(write);if(read)CloseHandle(read);if(process){if(WaitForSingleObject(process,0)!=WAIT_OBJECT_0)TerminateProcess(process,1);CloseHandle(process);}if(thread)CloseHandle(thread);}} handles;
    SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};
    check(CreatePipe(&handles.read,&handles.write,&security,4096)&&SetHandleInformation(handles.write,HANDLE_FLAG_INHERIT,0),"owned anonymous launch pipe");
    std::array<wchar_t,32768> path{};const auto length=GetModuleFileNameW(nullptr,path.data(),UINT(path.size()));
    check(length&&length<path.size(),"exact owned executable path");
    std::wstring command=L"\""+std::wstring(path.data())+L"\" --child";
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof startup;startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=handles.read;
    startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);PROCESS_INFORMATION process{};
    check(CreateProcessW(path.data(),mutableCommand.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process),"launch exactly the owned producer process");
    handles.process=process.hProcess;handles.thread=process.hThread;CloseHandle(handles.read);handles.read=nullptr;
    CrossProcess message{};message.source=handle(source.value);message.native=handle(native.value);message.compositorPid=process.dwProcessId;
    check(channel.duplicateToVerifiedPeer(handles.process,message.launch),"private section duplicated to the known owned child instance");
    const std::uint64_t lease=(std::uint64_t(7)<<32)|77;channel.sourceLease(lease);
    Registry registry{};registry.sourceLease=lease;registry.revision=1;
    registry.retained[0]={message.source,21,77,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    registry.retained[1]={message.native,22,message.compositorPid,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,1,1};
    std::array<ID3D11Texture2D*,ChannelAllocations> pins{};pins[0]=source.value;pins[1]=native.value;
    check(channel.publishRetained(registry,pins),"parent kernel allocations publish the only source registry");
    DWORD sent=0;check(WriteFile(handles.write,&message,sizeof message,&sent,nullptr)&&sent==sizeof message,"private fixture facts sent without public mapping names");
    const auto deadline=GetTickCount64()+5000;
    while(InterlockedCompareExchange(&channel.page()->peerReady,0,0)!=1&&GetTickCount64()<deadline&&WaitForSingleObject(handles.process,0)==WAIT_TIMEOUT)Sleep(1);
    check(consumer.prepare(device.value),"consumer prepares only for its bound private peer");
    const auto& allocation=registry.retained[1];
    check(!consumer.select(device.value,allocation,message.native,lease).view,"unowned native image never selects a latest mask");
    check(sync->AcquireSync(3,5000)==S_OK,"original native consumer acquires the producer release key");
    consumer.poll(device.value);const auto selected=consumer.select(device.value,allocation,message.native,lease);
    check(selected.view&&selected.lease==lease&&selected.scenePid==77&&consumer.nativeProducer(message.compositorPid),"real cross-process pair admitted under original native ownership");
    Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> mask;selected.view->GetResource(resource.out());
    check(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(mask.out()))),"selected view is the exact imported mask allocation");
    const auto rgb=pixels(device.value,context.value,native.value),alpha=pixels(device.value,context.value,mask.value);
    for(UINT i=0;i<width*height;++i){check(alpha[i*4+3]==input[i*4+3]&&rgb[i*4+3]==255,"exact zero/fractional/opaque alpha and unchanged native alpha cross the actual process boundary");
        for(unsigned c=0;c<3;++c)check(rgb[i*4+c]==input[i*4+c]&&!alpha[i*4+c],"every native RGB byte and private alpha-only payload retained across processes");}
    auto wrong=allocation;++wrong.generation;check(!consumer.select(device.value,wrong,message.native,lease).view,"wrong native allocation generation is refused");
    wrong=allocation;++wrong.pid;check(!consumer.select(device.value,wrong,message.native,lease).view,"different actual native owner is refused");
    check(!consumer.select(device.value,allocation,message.source,lease).view,"another sync allocation cannot seal this consumer image");
    channel.sourceLease(0);check(!consumer.select(device.value,allocation,message.native,lease).view,"revoked source lease cannot select a retained pair");
    context->Flush();check(sync->ReleaseSync(0)==S_OK,"consumer releases original ownership after its exact paired copy");
    BYTE finished=1;check(WriteFile(handles.write,&finished,1,&sent,nullptr)&&sent==1,"child may retire only after parent copy acknowledgement");
    check(WaitForSingleObject(handles.process,5000)==WAIT_OBJECT_0,"owned producer exits after acknowledgement");DWORD result=1;
    check(GetExitCodeProcess(handles.process,&result)&&result==0,"actual child producer completed its native path");
    consumer.stop();
}

}
int main(int argc,char** argv){try{
    if(argc==2&&!std::strcmp(argv[1],"--child")){
        CrossProcess message{};DWORD received=0;
        check(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&message,sizeof message,&received,nullptr)&&received==sizeof message,"owned launch pipe payload");
        ChannelPeer peer;check(peer.open(message.launch),"child binds the actual private kernel channel");run(&message,peer.page());
    }else if(argc==2&&!std::strcmp(argv[1],"--consumer"))consumerControl();
    else {check(argc==1,"known fixture mode");run();}
    std::printf("PASS %u production alpha ancestry/ownership/pixel checks; no headset result\n",checks);return 0;}
catch(const std::exception& error){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,error.what());return 1;}}
