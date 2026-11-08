#include "stream-bridge-sender-provider-directmode.h"
#include <cmath>
#include <limits>
namespace stream_bridge::sender {
std::atomic<DirectModeBoundary*> DirectModeBoundary::instance_{nullptr};
thread_local const DirectFrame* DirectModeBoundary::current_=nullptr;
namespace {
bool description(const DirectMode::SwapTextureSetDesc_t& d){SharedTextureShape shape{};return decodeSwapTextureSetDesc(d,shape);}
bool geometry(const DirectMode::SubmitLayerPerEye_t& e){
    const auto& b=e.bounds;
    if(!std::isfinite(b.uMin)||!std::isfinite(b.uMax)||!std::isfinite(b.vMin)||!std::isfinite(b.vMax)||b.uMin<0||b.uMax>1||b.vMin<0||b.vMax>1||b.uMax<=b.uMin||b.vMax<=b.vMin||!std::isfinite(e.flHmdPosePredictionTimeInSecondsFromNow))return false;
    for(const auto& row:e.mProjection.m)for(float x:row)if(!std::isfinite(x))return false;
    for(const auto& row:e.mHmdPose.m)for(float x:row)if(!std::isfinite(x))return false;
    return true;
}
struct Restore {const DirectFrame*& destination;const DirectFrame* previous;~Restore(){destination=previous;}};
}
bool DirectModeBoundary::prepare(const DirectModeFunctions& f)noexcept{
    if(!f.create||!f.destroy||!f.destroyAll||!f.next||!f.submit||!f.present||!f.post||!f.timing)return false;
    std::lock_guard<std::mutex> lock(mutex_);if(prepared_)return false;
    auto* expected=static_cast<DirectModeBoundary*>(nullptr);
    if(!instance_.compare_exchange_strong(expected,this)&&expected!=this)return false;
    original_=f;prepared_=true;enabled_=true;return true;
}
DirectModeFunctions DirectModeBoundary::entryPoints()const noexcept{return {create,destroy,destroyAll,next,submit,present,post,timing};}
void DirectModeBoundary::stop()noexcept{enabled_=false;++registryEpoch_;}
bool DirectModeBoundary::ownsProcess(std::uint32_t pid)const noexcept{std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock||!enabled_||!pid)return false;for(const auto& s:swaps_)if(s.pid==pid&&s.generation)return true;return false;}
std::uint64_t DirectModeBoundary::inventoryRevision()const noexcept{
    return enabled_.load(std::memory_order_acquire)&&!tainted_.load(std::memory_order_acquire)
        ?registryEpoch_.load(std::memory_order_acquire):0;
}
DirectInventory DirectModeBoundary::inventory()const noexcept{
    DirectInventory result;std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock||!enabled_||tainted_)return result;
    result.epoch=registryEpoch_;result.valid=true;
    for(unsigned i=0;i<swaps_.size();++i){const auto& s=swaps_[i];result.swaps[i]={s.component,s.pid,s.generation,s.description,s.handles};}return result;
}
DirectCleanupStats DirectModeBoundary::cleanupStats()const noexcept{
    return {destroyRequests_.load(),emptyDestroys_.load(),destroyChanges_.load(),destroyLockMisses_.load()};
}
void DirectModeBoundary::diagnose(DirectDiagnostic reason,std::uint32_t pid,const DirectMode::SwapTextureSetDesc_t* desc,
    const DirectMode::SubmitLayerPerEye_t* eye,std::uint32_t layers)noexcept{
    diagnosticCounts_[static_cast<unsigned>(reason)].fetch_add(1,std::memory_order_relaxed);
    if((!desc&&!eye)||diagnosticContext_.test_and_set(std::memory_order_acquire))return;
    DirectRejected event;event.reason=reason;event.pid=pid;event.layers=layers;
    if(desc){event.width=desc->nWidth;event.height=desc->nHeight;event.format=desc->nFormat;event.packedSamples=desc->nSampleCount;rejectedCreate_=event;}
    if(eye){event.bounds={eye->bounds.uMin,eye->bounds.vMin,eye->bounds.uMax,eye->bounds.vMax};rejectedSubmit_=event;}
    diagnosticContext_.clear(std::memory_order_release);
}
DirectDiagnosticStats DirectModeBoundary::diagnostics(std::uint32_t pid)const noexcept{
    DirectDiagnosticStats out;out.enabled=enabled_.load(std::memory_order_relaxed);
    for(unsigned i=0;i<out.counts.size();++i)out.counts[i]=diagnosticCounts_[i].load(std::memory_order_relaxed);
    if(!diagnosticContext_.test_and_set(std::memory_order_acquire)){
        out.create=rejectedCreate_;out.submit=rejectedSubmit_;out.contextAvailable=true;
        diagnosticContext_.clear(std::memory_order_release);
    }
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(lock){out.processSwaps=0;for(const auto& s:swaps_)if(s.generation&&s.pid==pid)++out.processSwaps;}
    return out;
}
const DirectFrame* DirectModeBoundary::currentFrame()noexcept{
    auto* self=instance_.load();if(!self)return nullptr;
    if(!self->enabled_){self->diagnose(DirectDiagnostic::Disabled);return nullptr;}
    if(!current_){self->diagnose(DirectDiagnostic::NoScope);return nullptr;}
    if(!current_->sourceKnown){self->diagnose(DirectDiagnostic::UnknownSource);return nullptr;}
    if(current_->registryEpoch!=self->registryEpoch_){self->diagnose(DirectDiagnostic::StaleEpoch);return nullptr;}
    return current_;
}
DirectModeBoundary::Component* DirectModeBoundary::component(void* value){for(auto& c:components_)if(c.value==value&&value)return &c;for(auto& c:components_)if(!c.value){c.value=value;return &c;}return nullptr;}
void DirectModeBoundary::create(void* object,std::uint32_t pid,const DirectMode::SwapTextureSetDesc_t* desc,DirectMode::SwapTextureSet_t* output){
    auto& self=*instance_.load();self.original_.create(object,pid,desc,output);
    if(!self.enabled_||!pid||!desc||!output)return;
    if(!description(*desc)){self.diagnose(DirectDiagnostic::CreateShape,pid,desc);return;}
    std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);if(!lock){self.diagnose(DirectDiagnostic::CreateLock,pid,desc);self.tainted_=true;return;}
    for(auto h:output->rSharedTextureHandles)if(!h){self.diagnose(DirectDiagnostic::CreateHandles,pid,desc);return;}
    if(output->rSharedTextureHandles[0]==output->rSharedTextureHandles[1]||output->rSharedTextureHandles[0]==output->rSharedTextureHandles[2]||output->rSharedTextureHandles[1]==output->rSharedTextureHandles[2]){self.diagnose(DirectDiagnostic::CreateHandles,pid,desc);return;}
    for(auto& s:self.swaps_)for(auto h:s.handles)for(auto wanted:output->rSharedTextureHandles)if(h&&h==wanted){self.diagnose(DirectDiagnostic::CreateDuplicate,pid,desc);self.tainted_=true;return;}
    if(self.nextSwap_==std::numeric_limits<std::uint64_t>::max()){self.diagnose(DirectDiagnostic::CreateCapacity,pid,desc);self.tainted_=true;return;}
    for(auto& s:self.swaps_)if(!s.generation){s.component=object;s.pid=pid;s.generation=++self.nextSwap_;s.description=*desc;for(unsigned i=0;i<3;++i)s.handles[i]=output->rSharedTextureHandles[i];++self.registryEpoch_;return;}
    self.diagnose(DirectDiagnostic::CreateCapacity,pid,desc);self.tainted_=true;
}
void DirectModeBoundary::destroy(void* object,vr::SharedTextureHandle_t handle){
    auto& self=*instance_.load();++self.destroyRequests_;
    {
        std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);
        if(lock){
            bool changed=false;
            for(auto& s:self.swaps_)if(s.generation&&s.component==object)
                for(auto h:s.handles)if(h&&h==handle){s={};changed=true;break;}
            // Cleanup of a client with no owned swap is not a registry change.
            // Publish a real removal before forwarding native destruction, so
            // in-flight sources still lose authority before their allocation.
            if(changed){++self.registryEpoch_;++self.destroyChanges_;}
            else ++self.emptyDestroys_;
        }else{++self.destroyLockMisses_;self.enabled_=false;++self.registryEpoch_;}
    }
    self.original_.destroy(object,handle);
}
void DirectModeBoundary::destroyAll(void* object,std::uint32_t pid){
    auto& self=*instance_.load();++self.destroyRequests_;
    {
        std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);
        if(lock){
            bool changed=false;
            for(auto& s:self.swaps_)if(s.generation&&s.component==object&&s.pid==pid){s={};changed=true;}
            if(changed){++self.registryEpoch_;++self.destroyChanges_;}
            else ++self.emptyDestroys_;
        }else{++self.destroyLockMisses_;self.enabled_=false;++self.registryEpoch_;}
    }
    self.original_.destroyAll(object,pid);
}
void DirectModeBoundary::next(void* object,vr::SharedTextureHandle_t handles[2],std::uint32_t(*indices)[2]){instance_.load()->original_.next(object,handles,indices);}
void DirectModeBoundary::submit(void* object,const DirectMode::SubmitLayerPerEye_t(&eyes)[2]){
    auto& self=*instance_.load();
    if(self.enabled_){std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);if(lock){if(auto c=self.component(object)){
        auto& frame=c->queued;frame.component=object;
        if(frame.layerCount>=4){self.diagnose(DirectDiagnostic::SubmitOverflow,0,nullptr,&eyes[0],frame.layerCount+1);frame.sourceKnown=false;self.tainted_=true;}
        else{auto& pair=frame.layers[frame.layerCount++];bool valid=true;
            for(unsigned eye=0;eye<2;++eye){pair[eye].submitted=eyes[eye];bool found=false;
                for(auto& s:self.swaps_)if(s.component==object&&s.generation)for(auto h:s.handles)if(h==eyes[eye].hTexture){pair[eye].description=s.description;pair[eye].swapGeneration=s.generation;pair[eye].processId=s.pid;found=true;}
                const bool mapped=found&&geometry(eyes[eye]);
                if(!found)self.diagnose(DirectDiagnostic::SubmitUnknown,0,nullptr,&eyes[eye],frame.layerCount);
                else if(!mapped)self.diagnose(DirectDiagnostic::SubmitGeometry,pair[eye].processId,nullptr,&eyes[eye],frame.layerCount);
                valid=valid&&mapped;}
            if(frame.layerCount==1)frame.sourceKnown=valid;else frame.sourceKnown=frame.sourceKnown&&valid;
            if(pair[0].processId&&pair[1].processId&&pair[0].processId!=pair[1].processId)self.diagnose(DirectDiagnostic::SubmitPid,pair[0].processId,nullptr,&eyes[0],frame.layerCount);
            frame.sourceKnown=frame.sourceKnown&&pair[0].processId==pair[1].processId;
        }
    }else{self.diagnose(DirectDiagnostic::ComponentCapacity);self.tainted_=true;}}else{self.diagnose(DirectDiagnostic::SubmitLock);self.tainted_=true;}}
    self.original_.submit(object,eyes);
}
void DirectModeBoundary::present(void* object,vr::SharedTextureHandle_t sync){
    auto& self=*instance_.load();DirectFrame frame{};
    {std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);if(lock&&self.enabled_){if(auto c=self.component(object)){
        frame=c->queued;c->queued={};frame.registryEpoch=self.registryEpoch_;
        if(self.nextPresent_!=std::numeric_limits<std::uint64_t>::max())frame.presentSerial=++self.nextPresent_;
        const bool tainted=self.tainted_.exchange(false);
        if(tainted)self.diagnose(DirectDiagnostic::PresentTainted);
        if(!frame.layerCount)self.diagnose(DirectDiagnostic::PresentEmpty);
        frame.sourceKnown=frame.sourceKnown&&frame.layerCount&&frame.presentSerial&&!tainted;
        c->presented=frame;
    }}else{self.diagnose(lock?DirectDiagnostic::Disabled:DirectDiagnostic::PresentLock);self.tainted_=true;}}
    Restore restore{current_,current_};current_=&frame;self.original_.present(object,sync);
}
void DirectModeBoundary::post(void* object,const DirectMode::Throttling_t* throttling){
    auto& self=*instance_.load();DirectFrame frame{};
    {std::unique_lock<std::mutex> lock(self.mutex_,std::try_to_lock);if(lock&&self.enabled_){for(auto& c:self.components_)if(c.value==object){frame=c.presented;c.presented={};break;}}}
    Restore restore{current_,current_};current_=&frame;self.original_.post(object,throttling);
}
void DirectModeBoundary::timing(void* object,vr::DriverDirectMode_FrameTiming* timing){instance_.load()->original_.timing(object,timing);}
} // namespace stream_bridge::sender
