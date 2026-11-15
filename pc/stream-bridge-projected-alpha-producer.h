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
        UINT coveredWidth=0,coveredHeight=0;
        std::array<MutexLease,8> scopes{};
        std::array<Reference<IDXGIResource>,8> sealed{};
        ImageWitness witness{};
    };
    std::array<ImageState,ChannelImages> images_{};
    std::atomic<unsigned> stage_{0},rootFailure_{0},inputFailure_{0},rootMisc_{0},rootKeyed_{0};
    std::atomic<std::uint64_t> rootPasses_{0},pairedDraws_{0};
    std::atomic<unsigned> fp16Calls_{0},fp16Prepared_{0},fp16Refusal_{0};
    std::atomic<unsigned> geometryReason_{0},geometryPixel_{0},geometryVertex_{0},geometryEmpty_{0};
    std::atomic<std::uint64_t> fp16ZeroClears_{0},fp16OtherClears_{0};
    ChannelPage* page_=nullptr;ID3D11Device* device_=nullptr;
    Replay replay_;Geometry geometry_;NativeShaders* shaders_=nullptr;MutexObserver* locks_=nullptr;
    std::uint64_t nextGeneration_=1;UINT uavSlots_=D3D11_PS_CS_UAV_REGISTER_COUNT;
    ImageState* find(ID3D11Resource* resource)noexcept{
        if(!resource)return nullptr;
        for(auto& image:images_)if(image.native.value==resource)return &image;
        return nullptr;
    }
    unsigned index(const ImageState& image)const noexcept{return image.row;}
    bool live(std::uint64_t lease)const noexcept{
        return page_&&lease&&!InterlockedCompareExchange(&page_->stopped,0,0)&&
            static_cast<std::uint64_t>(InterlockedCompareExchange64(&page_->sourceLease,0,0))==lease;
    }
    void revoke(ImageState& image)noexcept{
        image.valid=image.empty=false;image.coveredWidth=image.coveredHeight=0;image.lease=0;image.scene={};image.scopes={};image.witness={};
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
    static bool fp16(DXGI_FORMAT format)noexcept{
        return format==DXGI_FORMAT_R16G16B16A16_FLOAT||format==DXGI_FORMAT_R16G16B16A16_TYPELESS;
    }
    bool root(ID3D11Texture2D* texture,const D3D11_SHADER_RESOURCE_VIEW_DESC& view,Allocation& scene,std::uint64_t& lease)noexcept{
        const auto refuse=[this](unsigned reason)noexcept{rootFailure_=reason;return false;};
        if(texture){D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);rootMisc_=desc.MiscFlags;
            Reference<IDXGIKeyedMutex> keyed;rootKeyed_=SUCCEEDED(texture->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(keyed.out())))?1u:0u;}
        if(view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip||
            (view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
             view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))return refuse(1);
        Allocation actual{};if(!allocation(texture,actual)||actual.samples!=1||actual.arraySize!=1)return refuse(2);
        RegistryRead read;if(!read.acquire(page_))return refuse(3);const auto& registry=read.snapshot();
        if(!registry.sourceLease)return refuse(4);
        std::uint64_t luid=0;if(!registry.adapterKnown||!deviceLuid(device_,luid)||luid!=registry.adapterLuid)return refuse(5);
        bool sameHandle=false,sameOwner=false;
        for(const auto& known:registry.retained)if(known.handle==actual.handle){
            sameHandle=true;sameOwner|=known.pid==std::uint32_t(registry.sourceLease);
        }
        for(const auto& known:registry.retained)if(known.pid==std::uint32_t(registry.sourceLease)&&
            known.generation&&shapeMatches(known,actual)){
            // The actual application allocation must remain natively owned
            // through both the ordinary and companion samples. An unrelated
            // keyed mutex or a latest observed app texture is insufficient.
            if(!locks_->current(device_,known.handle)){stage_=1305;return refuse(9);}
            scene=known;lease=registry.sourceLease;if(!live(lease))return refuse(10);
            rootFailure_=0;rootPasses_.fetch_add(1,std::memory_order_relaxed);return true;
        }
        return refuse(!sameHandle?6:!sameOwner?7:8);
    }
    const ShaderFingerprint* policies_=nullptr;const unsigned* policyBytes_=nullptr;std::size_t policyCount_=0;
    bool boundedY(const NativeShaderDescription& program)const noexcept{
        if(program.kind!=NativeShader::MultiViewDownscaleY)return false;
        if(boundedPolicy_)return program.hash==*boundedPolicy_;
        // Both reviewed native Y variants use integer SV_Position.x and clamp
        // every t0 load row to [0,int(logical b0[0].y)-1]. Other programs have
        // no such permission, even if their constant prefix has the same size.
        return program.hash==shaderDigest("63faa7c4c75298a8c4f270d55a925c12af972cc23b557c823c2ba49b47de544e")||
            program.hash==shaderDigest("19d23135a8b403887043c106fb6aa998ad666a483959a9ee97a1d76bca744e69");
    }
    const std::array<std::uint8_t,32>* boundedPolicy_=nullptr;
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
    Producer(const ShaderFingerprint* policies=nullptr,const unsigned* bytes=nullptr,std::size_t count=0,const std::array<std::uint8_t,32>* boundedPolicy=nullptr)noexcept:
        policies_(policies),policyBytes_(bytes),policyCount_(policies&&bytes&&count<=32?count:0),boundedPolicy_(boundedPolicy){}
    bool prepare(ID3D11Device* device,ChannelPage* page,NativeShaders& shaders,MutexObserver& locks)noexcept{
        if(device_||!device||!page)return false;
        if(!replay_.prepare(device,{kernels::Clone64.data(),kernels::Clone64.size()},
            {kernels::Clone80.data(),kernels::Clone80.size()},{kernels::Clone32.data(),kernels::Clone32.size()}))return false;
        device_=device;page_=page;shaders_=&shaders;locks_=&locks;
        uavSlots_=device->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        stage_=1;return true;
    }
    unsigned stage()const noexcept{return stage_.load(std::memory_order_relaxed);}
    struct Diagnostics {
        unsigned rootFailure=0,inputFailure=0;std::uint64_t rootPasses=0,pairedDraws=0;
        unsigned fp16Calls=0,fp16Prepared=0,fp16Refusal=0;
        std::uint64_t rgbaBytes=0,fp16Bytes=0;
        unsigned geometryReason=0,geometryPixel=0,geometryVertex=0,geometryEmpty=0;
        std::uint64_t fp16ZeroClears=0,fp16OtherClears=0;
        unsigned rootMisc=0,rootKeyed=0;
    };
    Diagnostics diagnostics()const noexcept{
        return {rootFailure_.load(std::memory_order_relaxed),inputFailure_.load(std::memory_order_relaxed),
            rootPasses_.load(std::memory_order_relaxed),pairedDraws_.load(std::memory_order_relaxed),
            fp16Calls_.load(std::memory_order_relaxed),fp16Prepared_.load(std::memory_order_relaxed),fp16Refusal_.load(std::memory_order_relaxed),
            page_?std::uint64_t(InterlockedCompareExchange64(&page_->companionBytes,0,0)):0,
            page_?std::uint64_t(InterlockedCompareExchange64(&page_->fp16CompanionBytes,0,0)):0,
            geometryReason_.load(std::memory_order_relaxed),geometryPixel_.load(std::memory_order_relaxed),
            geometryVertex_.load(std::memory_order_relaxed),geometryEmpty_.load(std::memory_order_relaxed),
            fp16ZeroClears_.load(std::memory_order_relaxed),fp16OtherClears_.load(std::memory_order_relaxed),
            rootMisc_.load(std::memory_order_relaxed),rootKeyed_.load(std::memory_order_relaxed)};
    }
    Geometry& geometry()noexcept{return geometry_;}
    void depthState(ID3D11DepthStencilState* state)noexcept{replay_.observeDepth(state);}
    // Called only on native resource creation, never from raster callbacks.
    void target(ID3D11RenderTargetView* view)noexcept{
        if(!device_||!page_||!view)return;
        Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> texture;view->GetResource(resource.out());
        if(!resource.value||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out()))))return;
        D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};view->GetDesc(&targetDesc);
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        // Native FP16 storage can be typeless. Its actual FLOAT RTV, not the
        // storage token alone, establishes the typed interpretation.
        const bool floating=fp16(desc.Format);
        if(floating)fp16Calls_.fetch_add(1,std::memory_order_relaxed);
        const auto refuse=[this,floating](unsigned reason)noexcept{if(floating)fp16Refusal_=reason;};
        if(find(resource.value)){if(floating)fp16Refusal_=0;return;}
        if(desc.MipLevels!=1||desc.ArraySize!=1||desc.Usage!=D3D11_USAGE_DEFAULT||desc.CPUAccessFlags){refuse(2);return;}
        if(!(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE)){refuse(3);return;}
        if(desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS){refuse(4);return;}
        if((desc.SampleDesc.Count!=1&&desc.SampleDesc.Count!=4)||desc.Width<32||desc.Height<32){refuse(5);return;}
        const bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||
            desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if((!floating&&!rgba)||(floating&&(desc.SampleDesc.Count!=1||targetDesc.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))){refuse(6);return;}
        if(targetDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D&&targetDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS){refuse(7);return;}
        const auto bytes=std::uint64_t(desc.Width)*desc.Height*desc.SampleDesc.Count*(floating?8:4);
        for(auto& image:images_)if(!image.native.value){
            CompanionReservation reservation;
            if(!reservation.claim(page_,floating,bytes)){refuse(8);return;}
            Allocation native{};const bool shared=allocation(texture.value,native)&&desc.SampleDesc.Count==1&&!floating;
            if(!image.companion.prepare(device_,desc.Width,desc.Height,shared,desc.SampleDesc.Count,desc.SampleDesc.Quality,
                floating?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM)){
                image.companion.reset();refuse(9);return;}
            Reference<ID3D11DeviceContext> context;device_->GetImmediateContext(context.out());
            if(!image.companion.clear(context.value)){image.companion.reset();refuse(10);return;}
            image.native.value=texture.value;texture.value=nullptr;image.shape=desc;image.shared=shared;
            reservation.commit();
            image.generation=nextGeneration_++;stage_=2;
            if(floating){fp16Prepared_.fetch_add(1,std::memory_order_relaxed);fp16Refusal_=0;}
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
        refuse(11);
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{
        geometry_.invalidate(resource);
        for(auto& image:images_)if(image.native.value&&(!resource||image.native.value==resource))revoke(image);
    }
    void dispatch(ID3D11DeviceContext* context)noexcept{
        // Compute writes only its currently bound UAV resources. Revoking
        // unrelated RTV images and vertex bytes permanently lost native quad
        // evidence after an ordinary compositor compute pass.
        if(!device_||!context)return;
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> views{};
        context->CSGetUnorderedAccessViews(0,uavSlots_,views.data());
        for(auto* view:views)if(view){
            Reference<ID3D11Resource> resource;view->GetResource(resource.out());
            invalidate(resource.value);view->Release();
        }
    }
    void targets(ID3D11DeviceContext* context)noexcept{
        std::array<ID3D11RenderTargetView*,8> views{};context->OMGetRenderTargets(UINT(views.size()),views.data(),nullptr);
        for(auto* view:views)if(view){Reference<ID3D11Resource> resource;view->GetResource(resource.out());invalidate(resource.value);view->Release();}
    }
    void clear(ID3D11DeviceContext* context,ID3D11RenderTargetView* view,const FLOAT* color)noexcept{
        Reference<ID3D11Resource> resource;if(view)view->GetResource(resource.out());auto* image=find(resource.value);
        if(!image)return;
        revoke(*image);
        const bool zero=color&&!color[0]&&!color[1]&&!color[2]&&!color[3];
        if(fp16(image->shape.Format)){
            if(zero)fp16ZeroClears_.fetch_add(1,std::memory_order_relaxed);
            else fp16OtherClears_.fetch_add(1,std::memory_order_relaxed);
        }
        // Exact zero RGBA clear is public evidence that untouched regions have
        // no game contribution, including additive RGB at alpha zero.
        if(zero){
            image->empty=image->companion.clear(context);image->valid=false;
        }
    }
    void clearView(ID3D11DeviceContext* context,ID3D11View* view,const FLOAT* color,const D3D11_RECT* rectangles,UINT count)noexcept{
        Reference<ID3D11Resource> resource;if(view)view->GetResource(resource.out());auto* image=find(resource.value);
        if(!image){invalidate(resource.value);return;}
        Reference<ID3D11RenderTargetView> target;
        if(!view||FAILED(view->QueryInterface(__uuidof(ID3D11RenderTargetView),reinterpret_cast<void**>(target.out())))){
            invalidate(resource.value);return;}
        // No rectangles clears the complete actual RTV. A single enclosing
        // rectangle is equivalent; partial or multiple regions remain unknown.
        const bool whole=(!count&&!rectangles)||(count==1&&rectangles&&rectangles[0].left<=0&&rectangles[0].top<=0&&
            rectangles[0].right>=LONG(image->shape.Width)&&rectangles[0].bottom>=LONG(image->shape.Height));
        if(whole)clear(context,target.value,color);else invalidate(resource.value);
    }
    // Called BEFORE each ordinary Draw. Companion work follows on the same
    // context before original ReleaseSync; later writes revoke the witness.
    void draw(ID3D11DeviceContext* context,Draw original,UINT count,UINT start)noexcept{
        Reference<ID3D11RenderTargetView> targetView;context->OMGetRenderTargets(1,targetView.out(),nullptr);
        Reference<ID3D11Resource> output;if(targetView.value)targetView->GetResource(output.out());
        auto* destination=find(output.value);if(!destination)return;stage_=10;
        const bool empty=destination->empty;
        const bool priorComplete=destination->valid&&destination->coveredWidth==destination->shape.Width&&
            destination->coveredHeight==destination->shape.Height&&live(destination->lease)&&owned(*destination);
        const auto priorScene=destination->scene;const auto priorLease=destination->lease;
        revoke(*destination);
        D3D11_RENDER_TARGET_VIEW_DESC actualTarget{};targetView->GetDesc(&actualTarget);
        if(fp16(destination->shape.Format)&&actualTarget.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return;
        Reference<ID3D11PixelShader> pixel;Reference<ID3D11VertexShader> vertex;UINT pixelClasses=0,vertexClasses=0;
        context->PSGetShader(pixel.out(),nullptr,&pixelClasses);context->VSGetShader(vertex.out(),nullptr,&vertexClasses);
        NativeShaderDescription program{};
        if(pixelClasses||vertexClasses||!pixel.value||!vertex.value||!shaders_->describe(pixel.value,program)||program.linked)return;
        const auto kind=program.kind;const auto vertexKind=shaders_->identify(vertex.value);
        const bool x=kind==NativeShader::MultiViewDownscaleX||kind==NativeShader::MultiViewReprojectX;
        const bool y=kind==NativeShader::MultiViewDownscaleY||kind==NativeShader::MultiViewReprojectY;
        const bool inner=kind==NativeShader::MultiViewInner,resolve=kind==NativeShader::MultiViewResolve;
        if(!x&&!y&&!inner&&!resolve)return;
        stage_=11;
        const bool fullVertex=vertexKind==NativeShader::StreamVertex||vertexKind==NativeShader::MultiViewVertexY;
        unsigned geometryReason=0;
        const bool full=fullVertex&&geometry_.fullUnitQuad(context,count,start,destination->shape.Width,destination->shape.Height,destination->shape.SampleDesc.Count,&geometryReason);
        geometryReason_=geometryReason;geometryPixel_=unsigned(kind);geometryVertex_=unsigned(vertexKind);geometryEmpty_=unsigned(empty);
        stage_=!full&&fullVertex?2000+geometryReason:1000+2*unsigned(vertexKind)+(full?1:0);
        Reference<ID3D11GeometryShader> geometry;Reference<ID3D11HullShader> hull;Reference<ID3D11DomainShader> domain;
        context->GSGetShader(geometry.out(),nullptr,nullptr);context->HSGetShader(hull.out(),nullptr,nullptr);context->DSGetShader(domain.out(),nullptr,nullptr);
        if(geometry.value||hull.value||domain.value)return;
        UINT coveredWidth=destination->shape.Width,coveredHeight=destination->shape.Height;
        const bool rectangle=!full&&kind==NativeShader::MultiViewDownscaleX&&fullVertex&&
            geometry_.viewportQuad(context,count,start,destination->shape.Width,destination->shape.Height,
                destination->shape.SampleDesc.Count,coveredWidth,coveredHeight);
        const bool clearedReprojection=!full&&kind==NativeShader::MultiViewReprojectX&&
            vertexKind==NativeShader::MultiViewVertexX&&count==4&&!start&&(empty||priorComplete);
        if(!full&&!rectangle&&!clearedReprojection)return;
        stage_=12;
        Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11Resource> source;Reference<ID3D11Texture2D> sourceTexture;
        context->PSGetShaderResources(0,1,sourceView.out());D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        if(!sourceView.value)return;
        sourceView->GetDesc(&view);sourceView->GetResource(source.out());
        if(!source.value||FAILED(source->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(sourceTexture.out()))))return;
        stage_=13;Allocation scene{};std::uint64_t lease=0;auto* input=find(source.value);
        if(x||inner){if(!root(sourceTexture.value,view,scene,lease))return;
            if(kind==NativeShader::MultiViewReprojectX){
                // All three reviewed native reads must belong to the same
                // retained current source; t0 alone cannot authorize t1/t2.
                for(UINT slot=1;slot<3;++slot){
                    Reference<ID3D11ShaderResourceView> other;context->PSGetShaderResources(slot,1,other.out());
                    Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> texture;
                    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};Allocation known{};std::uint64_t otherLease=0;
                    if(!other.value){stage_=2022;return;}other->GetDesc(&desc);other->GetResource(resource.out());
                    if(!resource.value||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||
                        !root(texture.value,desc,known,otherLease)||otherLease!=lease||known.generation!=scene.generation||
                        known.pid!=scene.pid||!shapeMatches(known,scene)){stage_=2022;return;}
                }
            }
        }
        else {
            const unsigned failure=!input?1:!input->valid?2:!live(input->lease)?3:!owned(*input)?4:0;
            inputFailure_=failure;if(failure)return;
            scene=input->scene;lease=input->lease;
            if(input->coveredWidth!=input->shape.Width||input->coveredHeight!=input->shape.Height){
                std::array<float,4> words{};
                if(!boundedY(program)){stage_=2020;return;}
                if(!geometry_.constantPrefix(context,words.data(),sizeof words)){stage_=2031;return;}
                if(!std::isfinite(words[1])||words[1]<1||words[1]>float(input->coveredHeight)||words[1]!=std::floor(words[1])){stage_=2032;return;}
                if(destination->shape.Width>input->coveredWidth){stage_=2033;return;}
            }
        }
        const bool completeBase=empty||(priorComplete&&priorLease==lease&&priorScene.generation==scene.generation&&
            priorScene.pid==scene.pid&&shapeMatches(priorScene,scene));
        // A tested subset draw can extend an already complete, currently owned
        // image. Its depth/stencil comparison is replayed read-only before the
        // ordinary draw updates the native attachment once.
        const bool conditional=Replay::conditionalDepth(context);
        if((conditional||clearedReprojection)&&!completeBase){stage_=2021;return;}
        if(conditional&&!full&&!clearedReprojection){stage_=2021;return;}
        if(completeBase){coveredWidth=destination->shape.Width;coveredHeight=destination->shape.Height;}
        stage_=14;std::array<MutexLease,8> scopes{};if(!locks_->capture(device_,scopes)||!live(lease))return;
        stage_=15;bool okay=false;
        if(x&&fp16(destination->shape.Format))
            okay=replay_.intermediate(context,destination->companion,pixel.value,original,count,start);
        else if(y&&input&&fp16(input->shape.Format))
            okay=replay_.filtered(context,destination->companion,input->companion,pixel.value,prefix(program),original,count,start);
        else if(inner)okay=replay_.replay(context,destination->companion,pixel.value,prefix(program),original,count,start);
        else if(resolve&&input)okay=replay_.resolve(context,destination->companion,input->companion,pixel.value,original,count,start);
        if(!okay||!live(lease))return;
        stage_=16;destination->valid=true;destination->coveredWidth=coveredWidth;destination->coveredHeight=coveredHeight;destination->scene=scene;destination->lease=lease;destination->scopes=scopes;
        pairedDraws_.fetch_add(1,std::memory_order_relaxed);
    }
    void releasing(const MutexLease& scope,std::uint64_t key)noexcept{
        if(!page_||scope.device!=device_||!scope.retained)return;
        for(auto& image:images_)if(image.shared&&image.valid&&image.coveredWidth==image.shape.Width&&
            image.coveredHeight==image.shape.Height&&live(image.lease)){
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
            page_->witnesses[index(image)].publish(witness);stage_=20;
        }
    }
    void released(const MutexLease& scope,HRESULT result)noexcept{
        if(result==S_OK)return;
        for(auto& image:images_)for(const auto& candidate:image.scopes)
            if(candidate.allocation==scope.allocation&&candidate.epoch==scope.epoch){revoke(image);break;}
    }
};
} // namespace stream_bridge::sender::projected_alpha
