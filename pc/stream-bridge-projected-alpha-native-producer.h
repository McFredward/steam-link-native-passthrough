#pragma once
#include "stream-bridge-projected-alpha-producer.h"
#include <memory>

namespace stream_bridge::sender::projected_alpha {
// Captures alpha in the single original native raster operation. FP16 X is
// observed in place: its original RGBA already retains alpha. Scene Y/inner and
// resolve add a private MRT output, leaving native RGBA/constants unchanged.
class NativeProducer {
    static constexpr std::uint64_t MaximumBytes=512ull*1024*1024;
    struct Budget {std::atomic<std::uint64_t> bytes{0};std::atomic<unsigned> records{0};};
    struct Record final:IUnknown {
        inline static constexpr GUID Id{0xb895cb23,0x5ae3,0x41c0,{0xab,0xdf,0x92,0x16,0x63,0x45,0x6a,0x3c}};
        inline static std::atomic<std::uint64_t> next{1};
        std::atomic<ULONG> references{1};std::shared_ptr<Budget> budget;
        D3D11_TEXTURE2D_DESC shape{};Image alpha;Allocation native{},scene{};
        std::array<Allocation,8> contributors{};unsigned contributorCount=0;
        std::uint64_t serial=0,bytes=0,epoch=0,version=0,lease=0;
        UINT width=0,height=0;bool valid=false,empty=false;unsigned row=ChannelImages;
        std::array<MutexLease,8> scopes{};ImageWitness witness{};
        std::array<Reference<IDXGIResource>,8> sealed{};
        explicit Record(std::shared_ptr<Budget> value)noexcept:budget(std::move(value)),serial(next.fetch_add(1,std::memory_order_relaxed)){}
        ~Record(){budget->bytes.fetch_sub(bytes,std::memory_order_release);budget->records.fetch_sub(1,std::memory_order_release);}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{
            if(!out)return E_POINTER;
            *out=nullptr;
            if(iid!=IID_IUnknown&&iid!=Id)return E_NOINTERFACE;
            *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
        }
        ULONG STDMETHODCALLTYPE AddRef()override{return references.fetch_add(1,std::memory_order_relaxed)+1;}
        ULONG STDMETHODCALLTYPE Release()override{const auto n=references.fetch_sub(1,std::memory_order_acq_rel)-1;if(!n)delete this;return n;}
    };
    struct Export {Reference<Record> record;Reference<ID3D11Texture2D> native;Allocation allocation{};};
    std::array<Export,ChannelImages> exports_{};
    std::shared_ptr<Budget> budget_=std::make_shared<Budget>();
    ChannelPage* page_=nullptr;ID3D11Device* device_=nullptr;NativeShaders* shaders_=nullptr;MutexObserver* locks_=nullptr;
    Replay replay_;Geometry geometry_;std::uint64_t epoch_=1;
    std::atomic<unsigned> stage_{0},rootFailure_{0},inputFailure_{0},rootMisc_{0},rootKeyed_{0};
    std::atomic<std::uint64_t> roots_{0},pairs_{0},seals_{0};
    std::atomic<unsigned> fp16Calls_{0},fp16Prepared_{0},fp16Refusal_{0};
    std::atomic<unsigned> geometryReason_{0},geometryPixel_{0},geometryVertex_{0},geometryEmpty_{0};
    std::atomic<std::uint64_t> fp16Zero_{0},fp16Other_{0};
    const ShaderFingerprint* bounded_=nullptr;std::size_t boundedCount_=0;
    static bool fp16(DXGI_FORMAT format)noexcept{return format==DXGI_FORMAT_R16G16B16A16_FLOAT||format==DXGI_FORMAT_R16G16B16A16_TYPELESS;}
    static bool allocation(ID3D11Texture2D* texture,Allocation& value)noexcept{
        Reference<IDXGIResource> shared;HANDLE handle=nullptr;D3D11_TEXTURE2D_DESC desc{};
        if(!texture||FAILED(texture->QueryInterface(__uuidof(IDXGIResource),reinterpret_cast<void**>(shared.out())))||
            FAILED(shared->GetSharedHandle(&handle))||!handle)return false;
        texture->GetDesc(&desc);value={std::uint64_t(reinterpret_cast<std::uintptr_t>(handle)),0,0,desc.Width,desc.Height,
            unsigned(desc.Format),desc.SampleDesc.Count,desc.ArraySize};return true;
    }
    static bool same(const Allocation& a,const Allocation& b)noexcept{
        return a.handle==b.handle&&a.width==b.width&&a.height==b.height&&a.format==b.format&&a.samples==b.samples&&a.arraySize==b.arraySize;
    }
    static bool exact(const Allocation& a,const Allocation& b)noexcept{return same(a,b)&&a.pid==b.pid&&a.generation==b.generation;}
    static const Allocation* retained(const Registry& registry,const Allocation& value,std::uint32_t pid=0)noexcept{
        const Allocation* selected=nullptr;
        for(const auto& item:registry.retained)if(item.generation&&item.pid&&(!pid||item.pid==pid)&&same(item,value)){
            if(selected)return nullptr;
            selected=&item;
        }
        return selected;
    }
    bool live(std::uint64_t lease)const noexcept{
        return page_&&lease&&!InterlockedCompareExchange(&page_->stopped,0,0)&&
            std::uint64_t(InterlockedCompareExchange64(&page_->sourceLease,0,0))==lease;
    }
    Reference<Record> get(ID3D11Resource* resource)noexcept{
        Reference<Record> out;Reference<IUnknown> unknown;UINT size=sizeof(IUnknown*);
        if(resource&&SUCCEEDED(resource->GetPrivateData(Record::Id,&size,unknown.out()))&&size==sizeof(IUnknown*)&&unknown.value&&
            SUCCEEDED(unknown->QueryInterface(Record::Id,reinterpret_cast<void**>(out.out())))&&out->budget.get()==budget_.get())return out;
        if(out.value){out->Release();out.value=nullptr;}
        // Shared opens can be distinct native COM objects for the same pinned
        // kernel allocation. Export pins prevent numeric-handle recycling.
        Reference<ID3D11Texture2D> texture;Allocation actual{};
        if(!resource||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||!allocation(texture.value,actual))return out;
        for(auto& entry:exports_)if(entry.record.value&&entry.native.value&&same(entry.allocation,actual)){
            if(out.value){out->Release();out.value=nullptr;return out;}
            entry.record->AddRef();out.value=entry.record.value;
        }
        return out;
    }
    bool valid(const Record& record)const noexcept{return record.valid&&record.epoch==epoch_&&live(record.lease);}
    bool base(const Record& record)const noexcept{return (record.empty&&record.epoch==epoch_)||valid(record);}
    void revoke(Record& record)noexcept{
        record.valid=record.empty=false;record.width=record.height=0;record.epoch=0;record.lease=0;record.scene={};record.contributors={};record.contributorCount=0;record.scopes={};record.witness={};
        if(record.row<exports_.size()&&exports_[record.row].record.value==&record)(void)page_->witnesses[record.row].publish({});
        for(auto& sealed:record.sealed)if(sealed.value){sealed->Release();sealed.value=nullptr;}
        if(record.version==UINT64_MAX){InterlockedExchange(&page_->stopped,1);return;}++record.version;
    }
    bool root(ID3D11Texture2D* texture,const D3D11_SHADER_RESOURCE_VIEW_DESC& view,const Registry& registry,
        Allocation& scene,std::uint64_t& lease)noexcept{
        const auto refuse=[this](unsigned reason){rootFailure_=reason;return false;};
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);rootMisc_=desc.MiscFlags;
        Reference<IDXGIKeyedMutex> keyed;rootKeyed_=SUCCEEDED(texture->QueryInterface(__uuidof(IDXGIKeyedMutex),reinterpret_cast<void**>(keyed.out())))?1u:0u;
        if(view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Texture2D.MostDetailedMip||
            (view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM&&view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB&&
             view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM&&view.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))return refuse(1);
        Allocation actual{};if(!allocation(texture,actual)||actual.samples!=1||actual.arraySize!=1)return refuse(2);
        if(!registry.sourceLease)return refuse(4);
        std::uint64_t luid=0;if(!registry.adapterKnown||!deviceLuid(device_,luid)||luid!=registry.adapterLuid)return refuse(5);
        const auto* known=retained(registry,actual,std::uint32_t(registry.sourceLease));if(!known)return refuse(6);
        // This proof authorizes ONE original sample with an MRT output. It
        // deliberately cannot authorize a second, independently replayed draw.
        scene=*known;lease=registry.sourceLease;if(!live(lease))return refuse(10);
        rootFailure_=0;roots_.fetch_add(1,std::memory_order_relaxed);return true;
    }
    bool bounded(const NativeShaderDescription& program)const noexcept{
        if(bounded_){for(std::size_t i=0;i<boundedCount_;++i)if(bounded_[i].kind==program.kind&&bounded_[i].hash==program.hash)return true;return false;}
        return (program.kind==NativeShader::MultiViewDownscaleY&&
            (program.hash==shaderDigest("63faa7c4c75298a8c4f270d55a925c12af972cc23b557c823c2ba49b47de544e")||
             program.hash==shaderDigest("19d23135a8b403887043c106fb6aa998ad666a483959a9ee97a1d76bca744e69")))||
            (program.kind==NativeShader::MultiViewReprojectY&&
            (program.hash==shaderDigest("f81e7fc43b88c44c609d4cb478e61984c1ca0a0d28b29ad645a04a01b6ccc838")||
             program.hash==shaderDigest("675da9edd13d0f410d2f60e83446a483a653e1cdea0fcae1607791715c868a72")));
    }
    bool boundedInput(ID3D11DeviceContext* context,const NativeShaderDescription& program,const Record& input,UINT outputWidth)noexcept{
        if(outputWidth>input.width||!bounded(program))return false;
        std::array<float,4> dimensions{};
        if(program.kind==NativeShader::MultiViewDownscaleY){
            if(!geometry_.constantPrefix(context,dimensions.data(),sizeof dimensions))return false;
            return std::isfinite(dimensions[1])&&dimensions[1]>=1&&dimensions[1]<=float(input.height)&&dimensions[1]==std::floor(dimensions[1]);
        }
        // Reviewed ReprojectY reads x=int(SV_Position.x), and skips every row
        // outside roundEven(v4.yw). Its flat v4 comes from normalized native
        // view bounds times VS logical height. Checking normalized bounds and
        // the actual selected eye/view proves a containing integer read domain
        // without replacing native reprojection or approximating its matrices.
        std::array<std::uint32_t,76> viewState{};std::array<std::uint32_t,700> views{};
        if(!geometry_.constantPrefix(context,dimensions.data(),sizeof dimensions,true,0)||
            !geometry_.constantPrefix(context,viewState.data(),sizeof viewState,true,1)||
            !geometry_.constantPrefix(context,views.data(),sizeof views,true,2))return false;
        if(!std::isfinite(dimensions[1])||dimensions[1]<1||dimensions[1]>float(input.height)||dimensions[1]!=std::floor(dimensions[1])||
            viewState[72]>1||views[0]>2)return false;
        const auto offset=views[0]*58+(viewState[72]?28u:0u);float first=0,last=0;
        std::memcpy(&first,&views[(offset+28)*4+1],4);std::memcpy(&last,&views[(offset+30)*4+1],4);
        return std::isfinite(first)&&std::isfinite(last)&&first>=0&&first<=1&&last>=0&&last<=1;
    }
    static bool contribute(std::array<Allocation,8>& values,unsigned& count,const Allocation& value)noexcept{
        for(unsigned i=0;i<count;++i)if(exact(values[i],value))return true;
        if(count>=values.size())return false;
        values[count++]=value;return true;
    }
    bool exportImage(Record& record,ID3D11Texture2D* native,const Allocation& scene,std::uint64_t lease)noexcept{
        // Nonblocking exclusive gate also protects the consumer's cold import
        // against KMT-handle retirement/reuse. No graphics allocation occurs.
        if(InterlockedCompareExchange(&page_->registryGate,-1,0)!=0){stage_=4510;return false;}
        struct Unlock {ChannelPage* page;~Unlock(){InterlockedExchange(&page->registryGate,0);}} unlock{page_};
        Registry registry{};if(!page_->registry.read(registry)||registry.sourceLease!=lease||!live(lease)){stage_=4511;return false;}
        const auto* current=retained(registry,record.native,GetCurrentProcessId());
        const auto* currentScene=retained(registry,scene,std::uint32_t(lease));
        if(!current||!currentScene||!exact(*currentScene,scene)){stage_=4512;return false;}
        for(unsigned i=0;i<exports_.size();++i){auto& entry=exports_[i];if(!entry.record.value)continue;
            const auto* keep=retained(registry,entry.allocation,GetCurrentProcessId());
            if(keep&&exact(*keep,entry.allocation))continue;
            (void)page_->witnesses[i].publish({});(void)page_->registrations[i].publish({});
            entry.record->row=ChannelImages;entry.native->Release();entry.native.value=nullptr;
            entry.record->Release();entry.record.value=nullptr;entry.allocation={};
        }
        unsigned row=record.row;
        if(row>=exports_.size()||exports_[row].record.value!=&record){
            for(row=0;row<exports_.size()&&exports_[row].record.value;++row){}
            if(row==exports_.size()){stage_=4513;return false;}
            Allocation mask{};if(!allocation(record.alpha.resource(),mask)){stage_=4514;return false;}
            auto& entry=exports_[row];record.AddRef();entry.record.value=&record;native->AddRef();entry.native.value=native;entry.allocation=*current;
            record.row=row;MaskRegistration registration{*current,mask.handle,record.serial};
            (void)page_->witnesses[row].publish({});(void)page_->registrations[row].publish(registration);
        }
        return true;
    }
public:
    using Diagnostics=Producer::Diagnostics;
    NativeProducer(const ShaderFingerprint* boundedPolicies=nullptr,std::size_t count=0):
        bounded_(boundedPolicies),boundedCount_(boundedPolicies&&count<=32?count:0){}
    bool prepare(ID3D11Device* device,ChannelPage* page,NativeShaders& shaders,MutexObserver& locks)noexcept{
        if(device_||!device||!page||!budget_||!replay_.prepare(device,{kernels::Clone64.data(),kernels::Clone64.size()},
            {kernels::Clone80.data(),kernels::Clone80.size()},{kernels::Clone32.data(),kernels::Clone32.size()}))return false;
        device_=device;page_=page;shaders_=&shaders;locks_=&locks;stage_=4001;return true;
    }
    Geometry& geometry()noexcept{return geometry_;}
    void depthState(ID3D11DepthStencilState*)noexcept{} // Original attachment updates run once, never replayed.
    unsigned stage()const noexcept{return stage_.load(std::memory_order_relaxed);}
    std::uint64_t sealedVersions()const noexcept{return seals_.load(std::memory_order_relaxed);}
    Diagnostics diagnostics()const noexcept{
        return {rootFailure_.load(),inputFailure_.load(),roots_.load(),pairs_.load(),fp16Calls_.load(),fp16Prepared_.load(),fp16Refusal_.load(),
            budget_->bytes.load(),0,geometryReason_.load(),geometryPixel_.load(),geometryVertex_.load(),geometryEmpty_.load(),fp16Zero_.load(),fp16Other_.load(),rootMisc_.load(),rootKeyed_.load(),2,seals_.load(),budget_->records.load()};
    }
    void target(ID3D11RenderTargetView* view)noexcept{
        if(!device_||!view)return;
        Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> texture;view->GetResource(resource.out());
        if(!resource.value||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||get(resource.value).value)return;
        D3D11_TEXTURE2D_DESC desc{};D3D11_RENDER_TARGET_VIEW_DESC vd{};texture->GetDesc(&desc);view->GetDesc(&vd);const bool floating=fp16(desc.Format);
        if(floating)++fp16Calls_;
        const auto refuse=[this,floating](unsigned reason){if(floating)fp16Refusal_=reason;};
        if(desc.MipLevels!=1||desc.ArraySize!=1||desc.Usage!=D3D11_USAGE_DEFAULT||desc.CPUAccessFlags){refuse(2);return;}
        if(!(desc.BindFlags&D3D11_BIND_SHADER_RESOURCE)){refuse(3);return;}
        if(desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS){refuse(4);return;}
        if((desc.SampleDesc.Count!=1&&desc.SampleDesc.Count!=4)||desc.Width<32||desc.Height<32){refuse(5);return;}
        const bool rgba=desc.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM||desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if((!floating&&!rgba)||(floating&&(desc.SampleDesc.Count!=1||desc.MiscFlags||vd.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT))){refuse(6);return;}
        if(vd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D&&vd.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2DMS){refuse(7);return;}
        if(budget_->records.fetch_add(1,std::memory_order_acq_rel)>=1024){--budget_->records;refuse(11);return;}
        Reference<Record> record;record.value=new(std::nothrow) Record(budget_);if(!record.value){--budget_->records;refuse(9);return;}
        if(!record->serial){refuse(11);return;}record->shape=desc;(void)allocation(texture.value,record->native);
        if(!floating){
            const auto bytes=std::uint64_t(desc.Width)*desc.Height*desc.SampleDesc.Count*4;
            auto before=budget_->bytes.load(std::memory_order_relaxed);bool reserved=false;
            for(unsigned attempt=0;attempt<8;++attempt){
                if(bytes>MaximumBytes||before>MaximumBytes-bytes)break;
                if(budget_->bytes.compare_exchange_weak(before,before+bytes,std::memory_order_acq_rel)){reserved=true;break;}
            }
            if(!reserved){refuse(8);return;}record->bytes=bytes;
            if(!record->alpha.prepare(device_,desc.Width,desc.Height,record->native.handle!=0,desc.SampleDesc.Count,desc.SampleDesc.Quality)){refuse(9);return;}
        }
        if(FAILED(texture->SetPrivateDataInterface(Record::Id,record.value))){refuse(9);return;}
        if(floating){++fp16Prepared_;fp16Refusal_=0;}stage_=4002;
    }
    void invalidate(ID3D11Resource* resource=nullptr)noexcept{
        geometry_.invalidate(resource);
        if(resource){auto record=get(resource);if(record.value)revoke(*record.value);return;}
        if(epoch_==UINT64_MAX){InterlockedExchange(&page_->stopped,1);return;}++epoch_;
        for(auto& entry:exports_)if(entry.record.value)revoke(*entry.record.value);
    }
    void dispatch(ID3D11DeviceContext* context)noexcept{
        const UINT slots=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> views{};context->CSGetUnorderedAccessViews(0,slots,views.data());
        for(auto* view:views)if(view){Reference<ID3D11Resource> resource;view->GetResource(resource.out());invalidate(resource.value);view->Release();}
    }
    void targets(ID3D11DeviceContext* context)noexcept{
        Reference<ID3D11BlendState> blend;context->OMGetBlendState(blend.out(),nullptr,nullptr);D3D11_BLEND_DESC desc{};
        if(blend.value)blend->GetDesc(&desc);else desc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        std::array<ID3D11RenderTargetView*,8> views{};context->OMGetRenderTargets(UINT(views.size()),views.data(),nullptr);
        for(unsigned i=0;i<views.size();++i)if(views[i]){
            if(desc.RenderTarget[desc.IndependentBlendEnable?i:0].RenderTargetWriteMask){Reference<ID3D11Resource> resource;views[i]->GetResource(resource.out());invalidate(resource.value);}
            views[i]->Release();
        }
        const UINT slots=device_->GetFeatureLevel()>=D3D_FEATURE_LEVEL_11_1?D3D11_1_UAV_SLOT_COUNT:D3D11_PS_CS_UAV_REGISTER_COUNT;
        std::array<ID3D11UnorderedAccessView*,D3D11_1_UAV_SLOT_COUNT> writable{};
        context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,slots,writable.data());
        for(auto* view:writable)if(view){Reference<ID3D11Resource> resource;view->GetResource(resource.out());invalidate(resource.value);view->Release();}
    }
    void clear(ID3D11DeviceContext* context,ID3D11RenderTargetView* view,const FLOAT* color)noexcept{
        Reference<ID3D11Resource> resource;if(view)view->GetResource(resource.out());auto record=get(resource.value);if(!record.value)return;
        revoke(*record.value);const bool zero=color&&!color[0]&&!color[1]&&!color[2]&&!color[3];
        if(fp16(record->shape.Format)){if(zero)++fp16Zero_;else ++fp16Other_;}
        // A native compositor RGBA blank clear has no game contribution even
        // when its ordinary opaque-output policy clears A to1. FP16 is sampled
        // directly by native Y, so that intermediate requires genuine zero A.
        const bool blank=color&&!color[0]&&!color[1]&&!color[2]&&std::isfinite(color[3])&&(!fp16(record->shape.Format)||zero);
        if(blank&&(fp16(record->shape.Format)||record->alpha.clear(context))){record->empty=true;record->epoch=epoch_;record->width=record->shape.Width;record->height=record->shape.Height;}
    }
    void clearView(ID3D11DeviceContext* context,ID3D11View* view,const FLOAT* color,const D3D11_RECT* rectangles,UINT count)noexcept{
        Reference<ID3D11Resource> resource;if(view)view->GetResource(resource.out());auto record=get(resource.value);if(!record.value)return;
        Reference<ID3D11RenderTargetView> target;if(FAILED(view->QueryInterface(__uuidof(ID3D11RenderTargetView),reinterpret_cast<void**>(target.out())))){invalidate(resource.value);return;}
        const bool whole=(!rectangles&&!count)||(rectangles&&count==1&&rectangles[0].left<=0&&rectangles[0].top<=0&&rectangles[0].right>=LONG(record->shape.Width)&&rectangles[0].bottom>=LONG(record->shape.Height));
        if(whole)clear(context,target.value,color);else invalidate(resource.value);
    }
    // Returns true ONLY when the original Draw has already run exactly once.
    // The facade must forward it normally on false, never run it again on true.
    bool draw(ID3D11DeviceContext* context,Draw original,UINT count,UINT start)noexcept{
        Reference<ID3D11RenderTargetView> targetView;context->OMGetRenderTargets(1,targetView.out(),nullptr);
        Reference<ID3D11Resource> output;Reference<ID3D11Texture2D> outputTexture;if(targetView.value)targetView->GetResource(output.out());
        auto destination=get(output.value);if(!destination.value)return false;stage_=4010;
        Reference<ID3D11BlendState> blend;context->OMGetBlendState(blend.out(),nullptr,nullptr);D3D11_BLEND_DESC blendDesc{};
        if(blend.value){blend->GetDesc(&blendDesc);if(!blendDesc.RenderTarget[0].RenderTargetWriteMask)return false;}
        bool previous=base(*destination.value);
        if(previous&&destination->native.handle&&!destination->empty){
            bool owned=false;
            for(const auto& scope:destination->scopes)if(scope){
                owned=true;const auto current=locks_->current(device_,scope.allocation);
                if(!current||current.epoch!=scope.epoch){owned=false;break;}
            }
            previous=owned;
        }
        const auto priorScene=destination->scene;const auto priorLease=destination->lease;
        const auto priorContributors=destination->contributors;const auto priorContributorCount=destination->contributorCount;
        const auto priorWidth=destination->width,priorHeight=destination->height;const bool empty=destination->empty&&destination->epoch==epoch_;
        revoke(*destination.value);
        if(!output.value||FAILED(output->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(outputTexture.out()))))return false;
        D3D11_RENDER_TARGET_VIEW_DESC actualTarget{};targetView->GetDesc(&actualTarget);
        if(fp16(destination->shape.Format)&&actualTarget.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT)return false;
        Reference<ID3D11PixelShader> pixel;Reference<ID3D11VertexShader> vertex;UINT pc=0,vc=0;
        context->PSGetShader(pixel.out(),nullptr,&pc);context->VSGetShader(vertex.out(),nullptr,&vc);NativeShaderDescription program{};
        if(pc||vc||!pixel.value||!vertex.value||!shaders_->describe(pixel.value,program)||program.linked)return false;
        const auto kind=program.kind,vertexKind=shaders_->identify(vertex.value);
        const bool x=kind==NativeShader::MultiViewDownscaleX||kind==NativeShader::MultiViewReprojectX;
        const bool y=kind==NativeShader::MultiViewDownscaleY||kind==NativeShader::MultiViewReprojectY;
        const bool inner=kind==NativeShader::MultiViewInner,resolving=kind==NativeShader::MultiViewResolve;
        if(!x&&!y&&!inner&&!resolving)return false;
        if(kind==NativeShader::MultiViewReprojectY&&vertexKind!=NativeShader::MultiViewVertexY){stage_=4102;return false;}
        ID3D11PixelShader* capture=x?nullptr:shaders_->captureProgram(pixel.value);
        if(!x&&!capture){stage_=4101;return false;}
        unsigned reason=0;const bool flat=vertexKind==NativeShader::StreamVertex||vertexKind==NativeShader::MultiViewVertexY;
        const bool full=flat&&geometry_.fullUnitQuad(context,count,start,destination->shape.Width,destination->shape.Height,destination->shape.SampleDesc.Count,&reason);
        UINT width=destination->shape.Width,height=destination->shape.Height;
        const bool rectangle=!full&&kind==NativeShader::MultiViewDownscaleX&&flat&&geometry_.viewportQuad(context,count,start,width,height,destination->shape.SampleDesc.Count,width,height);
        const bool partialReproject=!full&&kind==NativeShader::MultiViewReprojectX&&vertexKind==NativeShader::MultiViewVertexX&&count==4&&!start&&previous;
        geometryReason_=reason;geometryPixel_=unsigned(kind);geometryVertex_=unsigned(vertexKind);geometryEmpty_=empty?1u:0u;
        if(!full&&!rectangle&&!partialReproject){stage_=4200+reason;return false;}
        Reference<ID3D11GeometryShader> gs;Reference<ID3D11HullShader> hs;Reference<ID3D11DomainShader> ds;
        context->GSGetShader(gs.out(),nullptr,nullptr);context->HSGetShader(hs.out(),nullptr,nullptr);context->DSGetShader(ds.out(),nullptr,nullptr);
        if(gs.value||hs.value||ds.value)return false;
        Reference<ID3D11ShaderResourceView> sourceView;Reference<ID3D11Resource> source;Reference<ID3D11Texture2D> sourceTexture;
        context->PSGetShaderResources(0,1,sourceView.out());D3D11_SHADER_RESOURCE_VIEW_DESC view{};if(!sourceView.value)return false;
        sourceView->GetDesc(&view);sourceView->GetResource(source.out());
        if(!source.value||FAILED(source->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(sourceTexture.out()))))return false;
        RegistryRead read;if(!read.acquire(page_)){rootFailure_=3;stage_=4303;return false;}
        Allocation scene{};std::uint64_t lease=0;auto input=get(source.value);
        std::array<Allocation,8> contributors{};unsigned contributorCount=0;
        if(x||inner){
            if(!root(sourceTexture.value,view,read.snapshot(),scene,lease)){stage_=4300+rootFailure_.load();return false;}
            if(!contribute(contributors,contributorCount,scene)){stage_=4323;return false;}
            if(kind==NativeShader::MultiViewReprojectX)for(UINT slot=1;slot<3;++slot){
                Reference<ID3D11ShaderResourceView> other;Reference<ID3D11Resource> resource;Reference<ID3D11Texture2D> texture;
                D3D11_SHADER_RESOURCE_VIEW_DESC otherView{};Allocation known{};std::uint64_t otherLease=0;
                context->PSGetShaderResources(slot,1,other.out());if(!other.value){stage_=4322;return false;}
                other->GetDesc(&otherView);other->GetResource(resource.out());
                if(!resource.value||FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(texture.out())))||
                    !root(texture.value,otherView,read.snapshot(),known,otherLease)||otherLease!=lease||known.pid!=scene.pid||
                    !contribute(contributors,contributorCount,known)){stage_=4322;return false;}
            }
        }else{
            const unsigned failure=!input.value?1u:!input->valid||input->epoch!=epoch_?2u:!live(input->lease)?3u:0u;inputFailure_=failure;
            if(failure){stage_=4400+failure;return false;}scene=input->scene;lease=input->lease;
            contributors=input->contributors;contributorCount=input->contributorCount;
            if(!fp16(input->shape.Format)&&!resolving){stage_=4405;return false;}
            if(resolving&&(input->shape.SampleDesc.Count!=4||input->width!=input->shape.Width||input->height!=input->shape.Height||
                input->shape.Width!=destination->shape.Width||input->shape.Height!=destination->shape.Height||view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2DMS)){stage_=4406;return false;}
            if(!resolving&&(view.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D||view.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT||view.Texture2D.MostDetailedMip)){stage_=4407;return false;}
            if((input->width!=input->shape.Width||input->height!=input->shape.Height)&&!boundedInput(context,program,*input.value,destination->shape.Width)){stage_=4431;return false;}
        }
        const bool matchingBase=previous&&(empty||(priorLease==lease&&priorScene.pid==scene.pid));
        if(matchingBase&&!empty)for(unsigned i=0;i<priorContributorCount;++i)
            if(!contribute(contributors,contributorCount,priorContributors[i])){stage_=4323;return false;}
        for(unsigned i=0;i<contributorCount;++i){
            const auto* current=retained(read.snapshot(),contributors[i],std::uint32_t(lease));
            if(!current||!exact(*current,contributors[i])){stage_=4324;return false;}
        }
        const bool conditional=Replay::conditionalDepth(context);
        if((conditional||partialReproject)&&!matchingBase){stage_=4421;return false;}
        if(matchingBase&&((conditional||partialReproject)||(priorWidth>=width&&priorHeight>=height))){width=priorWidth;height=priorHeight;}
        if(partialReproject&&(width==0||height==0)){stage_=4422;return false;}
        std::array<MutexLease,8> scopes{};
        if(destination->native.handle&&!locks_->capture(device_,scopes)){stage_=4440;return false;}
        read.release();
        if(destination->native.handle&&!exportImage(*destination.value,outputTexture.value,scene,lease))return false;
        bool executed=false;
        if(x&&fp16(destination->shape.Format)){
            // Native X itself writes genuine RGBA; observe its single original
            // operation rather than sampling an unsynchronised source twice.
            executed=replay_.nativeAlpha(context,pixel.value,0,false,original,count,start);
        }else if(!x)executed=replay_.nativeCapture(context,destination->alpha,pixel.value,capture,original,count,start,resolving?&input->alpha:nullptr);
        if(!executed){stage_=4450;return false;}
        if(live(lease)){
            destination->valid=true;destination->empty=false;destination->epoch=epoch_;destination->scene=scene;destination->lease=lease;
            destination->width=width;destination->height=height;destination->scopes=scopes;
            destination->contributors=contributors;destination->contributorCount=contributorCount;++pairs_;stage_=4016;
        }
        return true;
    }
    void releasing(const MutexLease& scope,std::uint64_t key)noexcept{
        if(!page_||scope.device!=device_||!scope.retained)return;
        for(auto& entry:exports_)if(entry.record.value){auto& record=*entry.record.value;
            if(!valid(record)||record.width!=record.shape.Width||record.height!=record.shape.Height)continue;
            unsigned choice=0;for(;choice<record.scopes.size();++choice)
                if(record.scopes[choice].allocation==scope.allocation&&record.scopes[choice].epoch==scope.epoch)break;
            if(choice==record.scopes.size())continue;
            RegistryRead read;if(!read.acquire(page_)||read.snapshot().sourceLease!=record.lease)continue;
            const auto* native=retained(read.snapshot(),entry.allocation,GetCurrentProcessId());
            const auto* scene=retained(read.snapshot(),record.scene,std::uint32_t(record.lease));
            if(!native||!scene||!exact(*native,entry.allocation)||!exact(*scene,record.scene))continue;
            bool current=record.contributorCount>0;
            for(unsigned i=0;i<record.contributorCount;++i){const auto* item=retained(read.snapshot(),record.contributors[i],std::uint32_t(record.lease));
                if(!item||!exact(*item,record.contributors[i]))current=false;
            }
            if(!current)continue;
            Allocation mask{};if(!allocation(record.alpha.resource(),mask))continue;
            auto& witness=record.witness;witness.native=*native;witness.scene=*scene;witness.sourceLease=record.lease;
            witness.maskHandle=mask.handle;witness.maskGeneration=record.serial;witness.imageVersion=record.version;witness.complete=1;witness.route=2;witness.contributors=record.contributors;witness.contributorCount=record.contributorCount;
            witness.releases[choice]={scope.allocation,scope.epoch,key};
            if(!record.sealed[choice].value){scope.retained->AddRef();record.sealed[choice].value=scope.retained;}
            if(page_->witnesses[record.row].publish(witness)){++seals_;stage_=4020;}
        }
    }
    void released(const MutexLease& scope,HRESULT result)noexcept{
        if(result==S_OK)return;
        for(auto& entry:exports_)if(entry.record.value)for(const auto& captured:entry.record->scopes)
            if(captured.allocation==scope.allocation&&captured.epoch==scope.epoch){revoke(*entry.record.value);break;}
    }
};
} // namespace stream_bridge::sender::projected_alpha
