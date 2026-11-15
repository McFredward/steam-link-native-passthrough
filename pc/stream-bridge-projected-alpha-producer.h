#pragma once
#include "stream-bridge-projected-alpha-channel.h"
#include "stream-bridge-projected-alpha-geometry.h"
#include "stream-bridge-projected-alpha-kernels.h"
#include "stream-bridge-sender-provider-shaders.h"

namespace stream_bridge::sender::projected_alpha {
// One controller per native immediate context. Creation callbacks prepare all
// storage. Draw callbacks perform only bounded metadata reads and GPU commands.
// Ordinary native resources, shaders, constant buffers and pixels are untouched.
class Producer {
    struct ImageState {
        Reference<ID3D11Texture2D> native;
        D3D11_TEXTURE2D_DESC shape{};
        Image companion;
        Allocation scene{};
        std::uint64_t handle=0,generation=0,version=0,lease=0;
        bool valid=false,empty=false,shared=false;unsigned row=ChannelImages;
        std::array<MutexLease,8> scopes{};
        std::array<Reference<IDXGIResource>,8> sealed{};
        ImageWitness witness{};
    };
    std::array<ImageState,ChannelImages> images_{};
    ChannelPage* page_=nullptr;ID3D11Device* device_=nullptr;
    Replay replay_;Geometry geometry_;NativeShaders* shaders_=nullptr;MutexObserver* locks_=nullptr;
    std::uint64_t nextGeneration_=1;
    static constexpr std::uint64_t Budget=256ull*1024*1024;
    ImageState* find(ID3D11Resource* resource)noexcept{
        for(auto& image:images_)if(image.native.value==resource)return &image;
        return nullptr;
    }
    unsigned index(const ImageState& image)const noexcept{return image.row;}
    bool live(std::uint64_t lease)const noexcept{
        return page_&&lease&&!InterlockedCompareExchange(&page_->stopped,0,0)&&
            static_cast<std::uint64_t>(InterlockedCompareExchange64(&page_->sourceLease,0,0))==lease;
    }
    void revoke(ImageState& image)noexcept{
        image.valid=image.empty=false;image.lease=0;image.scene={};image.scopes={};image.witness={};
        if(page_&&image.row<ChannelImages)page_->witnesses[index(image)].publish({});
        for(auto& retained:image.sealed)if(retained.value){retained->Release();retained.value=nullptr;}
        if(image.version==UINT64_MAX){page_=nullptr;return;}
        ++image.version;
    }
    bool owned(const ImageState& image)const noexcept{
        bool any=false;
        for(const auto& scope:image.scopes)if(scope){
            any=true;const auto now=locks_->current(device_,scope.allocation);
            if(!now||now.epoch!=scope.epoch)return false;
        }
        return any;
    }
    static bool allocation(ID3D11Texture2D* texture,Allocation& value)noexcept{
        Reference<IDXGIResource> shared;HANDLE handle=nullptr;D3D11_TEXTURE2D_DESC desc{};
        if(!texture||FAILED(texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))||
            FAILED(shared->GetSharedHandle(&handle))||!handle)return false;
        texture->GetDesc(&desc);value.handle=std::uint64_t(reinterpret_cast<std::uintptr_t>(handle));
        value.width=desc.Width;value.height=desc.Height;value.format=desc.Format;
        value.samples=desc.SampleDesc.Count;value.arraySize=desc.ArraySize;return true;
    }
    static bool shapeMatches(const Allocation& a,const Allocation& b)noexcept{
        return a.handle==b.handle&&a.width==b.width&&a.height==b.height&&a.format==b.format&&
            a.samples==b.samples&&a.arraySize==b.arraySize;
    }
    bool root(ID3D11Texture2D* texture,const D3D11_SHADER_RESOURCE_VIEW_DESC& view,Allocation& scene,std::uint64_t& lease)noexcept{
        if(view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip||
            (view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
             view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))return false;
        Allocation actual{};if(!allocation(texture,actual)||actual.samples!=1||actual.arraySize!=1)return false;
        RegistryRead read;if(!read.acquire(page_))return false;const auto& registry=read.snapshot();
        std::uint64_t luid=0;if(!registry.adapterKnown||!deviceLuid(device_,luid)||luid!=registry.adapterLuid)return false;
        for(const auto& known:registry.retained)if(known.pid==std::uint32_t(registry.sourceLease)&&
            known.generation&&shapeMatches(known,actual)){
            // The actual application allocation must remain natively owned
            // through both the ordinary and companion samples. An unrelated
            // keyed mutex or a latest observed app texture is insufficient.
            if(!locks_->current(device_,known.handle))return false;
            scene=known;lease=registry.sourceLease;return live(lease);
        }
        return false;
    }
    const ShaderFingerprint* policies_=nullptr;const unsigned* policyBytes_=nullptr;std::size_t policyCount_=0;
    unsigned prefix(const NativeShaderDescription& program)const noexcept{
        if(policies_){for(std::size_t i=0;i<policyCount_;++i)if(policies_[i].kind==program.kind&&policies_[i].hash==program.hash)return policyBytes_[i];return 0;}
        const auto& hash=program.hash;
        const std::array<const char*,6> reviewed{{
            "eb43d281e767a94ff2865b8f06720372b1f32365f1174298895910b6fe63f26c",
            "63faa7c4c75298a8c4f270d55a925c12af972cc23b557c823c2ba49b47de544e",
            "f81e7fc43b88c44c609d4cb478e61984c1ca0a0d28b29ad645a04a01b6ccc838",
            "f8dc71b5055fca7daeccf1e9fe62b804b1ea12bb1f5be758656bdcde5d44eb50",
            "19d23135a8b403887043c106fb6aa998ad666a483959a9ee97a1d76bca744e69",
            "675da9edd13d0f410d2f60e83446a483a653e1cdea0fcae1607791715c868a72"}};
        for(unsigned i=0;i<reviewed.size();++i)if(hash==shaderDigest(reviewed[i]))return i<3?80:64;
        return 0;
    }
public:
    // An explicit compiled contract table is also used by owned WARP fixtures;
    // shipping observers always use the reviewed native default above.
    Producer(const ShaderFingerprint* policies=nullptr,const unsigned* bytes=nullptr,std::size_t count=0)noexcept:
        policies_(policies),policyBytes_(bytes),policyCount_(policies&&bytes&&count<=32?count:0){}
    bool prepare(ID3D11Device* device,ChannelPage* page,NativeShaders& shaders,MutexObserver& locks)noexcept{
        if(device_||!device||!page)return false;
        if(!replay_.prepare(device,{kernels::Clone64.data(),kernels::Clone64.size()},
            {kernels::Clone80.data(),kernels::Clone80.size()},{kernels::Clone32.data(),kernels::Clone32.size()}))return false;
        device_=device;page_=page;shaders_=&shaders;locks_=&locks;return true;
    }
    Geometry& geometry()noexcept{return geometry_;}
    // Called only on native resource creation, never from raster callbacks.
    void target(ID3D11RenderTargetView* view)noexcept{
        if(!device_||!page_||!view)return;
        Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> texture;view->GetResource(resource.out());
        if(!resource.value||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||find(resource.value))return;
        D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};view->GetDesc(&targetDesc);
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(desc.MipLevels!=1||desc.ArraySize!=1||desc.Usage!=D3D11_USAGE_DEFAULT||desc.CPUAccessFlags||
            !(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE)||desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS||
            (desc.SampleDesc.Count!=1&&desc.SampleDesc.Count!=4)||desc.Width<32||desc.Height<32)return;
        const bool fp16=desc.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        const bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||
            desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if((!fp16&&!rgba)||(fp16&&desc.SampleDesc.Count!=1))return;
        if(targetDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D&&targetDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS)return;
        const auto bytes=std::uint64_t(desc.Width)*desc.Height*desc.SampleDesc.Count*(fp16?8:4);
        for(auto& image:images_)if(!image.native.value){
            const auto retained=InterlockedCompareExchange64(&page_->companionBytes,0,0);
            if(retained<0||std::uint64_t(retained)>Budget||bytes>Budget-std::uint64_t(retained)||
                InterlockedCompareExchange64(&page_->companionBytes,retained+LONG64(bytes),retained)!=retained)return;
            Allocation native{};const bool shared=allocation(texture.value,native)&&desc.SampleDesc.Count==1&&!fp16;
            if(!image.companion.prepare(device_,desc.Width,desc.Height,shared,desc.SampleDesc.Count,desc.SampleDesc.Quality,
                fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM)){
                image.companion.reset();InterlockedExchangeAdd64(&page_->companionBytes,-LONG64(bytes));return;}
            Reference<ID3D11DeviceContext> context;device_->GetImmediateContext(context.out());
            if(!image.companion.clear(context.value))return;
            image.native.value=texture.value;texture.value=nullptr;image.shape=desc;image.shared=shared;
            image.generation=nextGeneration_++;
            if(shared){
                const auto row=InterlockedIncrement(&page_->nextImageSlot)-1;
                if(row<0||unsigned(row)>=ChannelImages){image.shared=false;return;}
                image.row=unsigned(row);
                Allocation mask{};if(!allocation(image.companion.resource(),mask))return;
                image.handle=mask.handle;MaskRegistration registration{};registration.native=native;
                registration.maskHandle=image.handle;registration.maskGeneration=image.generation;
                page_->registrations[index(image)].publish(registration);}
            return;
        }
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{
        geometry_.invalidate(resource);
        for(auto& image:images_)if(image.native.value&&(!resource||image.native.value==resource))revoke(image);
    }
    void targets(ID3D11DeviceContext* context)noexcept{
        std::array<ID3D11RenderTargetView*,8> views{};context->OMGetRenderTargets(UINT(views.size()),views.data(),nullptr);
        for(auto* view:views)if(view){Reference<ID3D11Resource> resource;view->GetResource(resource.out());invalidate(resource.value);view->Release();}
    }
    void clear(ID3D11DeviceContext* context,ID3D11RenderTargetView* view,const FLOAT* color)noexcept{
        Reference<ID3D11Resource> resource;if(view)view->GetResource(resource.out());auto* image=find(resource.value);
        if(!image)return;
        revoke(*image);
        // Exact zero RGBA clear is public evidence that untouched regions have
        // no game contribution, including additive RGB at alpha zero.
        if(color&&!color[0]&&!color[1]&&!color[2]&&!color[3]){
            image->empty=image->companion.clear(context);image->valid=false;
        }
    }
    // Called BEFORE each ordinary Draw. Companion work follows on the same
    // context before original ReleaseSync; later writes revoke the witness.
    void draw(ID3D11DeviceContext* context,Draw original,UINT count,UINT start)noexcept{
        Reference<ID3D11RenderTargetView> targetView;context->OMGetRenderTargets(1,targetView.out(),nullptr);
        Reference<ID3D11Resource> output;if(targetView.value)targetView->GetResource(output.out());
        auto* destination=find(output.value);if(!destination)return;
        const bool empty=destination->empty;
        revoke(*destination);
        Reference<ID3D11PixelShader> pixel;Reference<ID3D11VertexShader> vertex;UINT pixelClasses=0,vertexClasses=0;
        context->PSGetShader(pixel.out(),nullptr,&pixelClasses);context->VSGetShader(vertex.out(),nullptr,&vertexClasses);
        NativeShaderDescription program{};
        if(pixelClasses||vertexClasses||!pixel.value||!vertex.value||!shaders_->describe(pixel.value,program)||program.linked)return;
        const auto kind=program.kind;const auto vertexKind=shaders_->identify(vertex.value);
        const bool x=kind==NativeShader::MultiViewDownscaleX||kind==NativeShader::MultiViewReprojectX;
        const bool y=kind==NativeShader::MultiViewDownscaleY||kind==NativeShader::MultiViewReprojectY;
        const bool inner=kind==NativeShader::MultiViewInner,resolve=kind==NativeShader::MultiViewResolve;
        if(!x&&!y&&!inner&&!resolve)return;
        const bool fullVertex=vertexKind==NativeShader::StreamVertex||vertexKind==NativeShader::MultiViewVertexY;
        const bool full=fullVertex&&geometry_.fullUnitQuad(context,count,start,destination->shape.Width,destination->shape.Height,destination->shape.SampleDesc.Count);
        Reference<ID3D11GeometryShader> geometry;Reference<ID3D11HullShader> hull;Reference<ID3D11DomainShader> domain;
        context->GSGetShader(geometry.out(),nullptr,nullptr);context->HSGetShader(hull.out(),nullptr,nullptr);context->DSGetShader(domain.out(),nullptr,nullptr);
        if(geometry.value||hull.value||domain.value)return;
        if(!full&&!empty)return;
        if(!full&&(kind!=NativeShader::MultiViewReprojectX||vertexKind!=NativeShader::MultiViewVertexX||count!=4||start))return;
        Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11Resource> source;Reference<ID3D11Texture2D> sourceTexture;
        context->PSGetShaderResources(0,1,sourceView.out());D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        if(!sourceView.value)return;
        sourceView->GetDesc(&view);sourceView->GetResource(source.out());
        if(!source.value||FAILED(source->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(sourceTexture.out()))))return;
        Allocation scene{};std::uint64_t lease=0;auto* input=find(source.value);
        if(x||inner){if(!root(sourceTexture.value,view,scene,lease))return;}
        else {
            if(!input||!input->valid||!live(input->lease)||!owned(*input))return;
            scene=input->scene;lease=input->lease;
        }
        std::array<MutexLease,8> scopes{};if(!locks_->capture(device_,scopes)||!live(lease))return;
        bool okay=false;
        if(x&&destination->shape.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)
            okay=replay_.intermediate(context,destination->companion,pixel.value,original,count,start);
        else if(y&&input&&input->shape.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)
            okay=replay_.filtered(context,destination->companion,input->companion,pixel.value,prefix(program),original,count,start);
        else if(inner)okay=replay_.replay(context,destination->companion,pixel.value,prefix(program),original,count,start);
        else if(resolve&&input)okay=replay_.resolve(context,destination->companion,input->companion,pixel.value,original,count,start);
        if(!okay||!live(lease))return;
        destination->valid=true;destination->scene=scene;destination->lease=lease;destination->scopes=scopes;
    }
    void releasing(const MutexLease& scope,std::uint64_t key)noexcept{
        if(!page_||scope.device!=device_||!scope.retained)return;
        for(auto& image:images_)if(image.shared&&image.valid&&live(image.lease)){
            unsigned choice=0;
            for(;choice<image.scopes.size();++choice)if(image.scopes[choice].allocation==scope.allocation&&image.scopes[choice].epoch==scope.epoch)break;
            if(choice==image.scopes.size())continue;
            Allocation native{};if(!allocation(image.native.value,native))continue;
            RegistryRead read;if(!read.acquire(page_)||read.snapshot().sourceLease!=image.lease)continue;
            for(const auto& known:read.snapshot().retained)if(shapeMatches(known,native)){native=known;break;}
            if(!native.generation||!native.pid)continue;
            auto& witness=image.witness;witness.native=native;witness.scene=image.scene;
            witness.sourceLease=image.lease;witness.maskHandle=image.handle;witness.maskGeneration=image.generation;
            witness.imageVersion=image.version;witness.complete=1;witness.route=1;
            witness.releases[choice]={scope.allocation,scope.epoch,key};
            if(!image.sealed[choice].value){scope.retained->AddRef();image.sealed[choice].value=scope.retained;}
            page_->witnesses[index(image)].publish(witness);
        }
    }
    void released(const MutexLease& scope,HRESULT result)noexcept{
        if(result==S_OK)return;
        for(auto& image:images_)for(const auto& candidate:image.scopes)
            if(candidate.allocation==scope.allocation&&candidate.epoch==scope.epoch){revoke(image);break;}
    }
};
} // namespace stream_bridge::sender::projected_alpha
