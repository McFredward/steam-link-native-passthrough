#include "stream_bridge_mask_hevc_pool.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include <array>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace stream_bridge_mask_hevc {
struct Pool::Impl {
    enum class Phase {Free,Queued,Running,Done};
    struct Slot {
        Phase phase=Phase::Free;std::uint64_t ticket=0,epoch=0;
        Input input{};std::vector<std::uint8_t> encoded;Result result;
    };
    mutable std::mutex gate;std::condition_variable available;
    std::array<Slot,Capacity> slots{};std::array<std::thread,Workers> threads;
    std::array<std::unique_ptr<Decoder>,Workers> decoders;
    std::array<Statistics,Workers> measurements{};
    bool prepared=false,stopping=true;std::uint64_t epoch=1,nextTicket=0;
    Wake wake=nullptr;void* context=nullptr;
    void run(unsigned index)noexcept{
        for(;;){
            Slot* slot=nullptr;
            {
                std::unique_lock<std::mutex> lock(gate);
                available.wait(lock,[&]{return stopping||std::any_of(slots.begin(),slots.end(),[](const auto& s){return s.phase==Phase::Queued;});});
                if(stopping)return;
                for(auto& candidate:slots)if(candidate.phase==Phase::Queued&&(!slot||candidate.ticket<slot->ticket))slot=&candidate;
                slot->phase=Phase::Running;
            }
            auto& result=slot->result;result.identity=slot->input;result.identity.payload={};
            result.status=decoders[index]->decodeIntra(slot->input,result.decoded);
            bool publish=false;
            {
                std::lock_guard<std::mutex> lock(gate);measurements[index]=decoders[index]->statistics();
                if(!stopping&&slot->epoch==epoch){slot->phase=Phase::Done;publish=true;}
                else *slot={};
            }
            if(publish&&wake)wake(context);
        }
    }
    bool prepare(const Libraries* libraries,Wake callback,void* owner){
        if(prepared)return true;
        if(std::any_of(threads.begin(),threads.end(),[](const auto& thread){return thread.joinable();}))return false;
        wake=callback;context=owner;measurements={};
        for(auto& decoder:decoders){decoder=std::make_unique<Decoder>();if(!(libraries?decoder->prepareCold(*libraries):decoder->prepareInstalledCold()))return false;}
        stopping=false;prepared=true;
        for(unsigned i=0;i<Workers;++i)threads[i]=std::thread(&Impl::run,this,i);
        return true;
    }
    void stop(){
        {std::lock_guard<std::mutex> lock(gate);stopping=true;prepared=false;}
        available.notify_all();for(auto& thread:threads)if(thread.joinable())thread.join();
        for(auto& decoder:decoders)decoder.reset();
        std::lock_guard<std::mutex> lock(gate);slots={};wake=nullptr;context=nullptr;
    }
};
Pool::Pool():impl_(new Impl){}
Pool::~Pool(){stopCold();}
bool Pool::prepareCold(const Libraries& libraries,Wake wake,void* context)noexcept{
    try{if(impl_->prepare(&libraries,wake,context))return true;}catch(...){}
    stopCold();return false;
}
bool Pool::prepareInstalledCold(Wake wake,void* context)noexcept{
    try{if(impl_->prepare(nullptr,wake,context))return true;}catch(...){}
    stopCold();return false;
}
bool Pool::ready()const noexcept{std::lock_guard<std::mutex> lock(impl_->gate);return impl_->prepared&&!impl_->stopping;}
bool Pool::submit(const Input& input)noexcept{
    try{
        const auto pixels=std::size_t(input.descriptor.codedWidth)*input.descriptor.codedHeight;
        if(!input.payload.data||input.payload.size<stream_bridge::HevcCoverageHeaderBytes||input.payload.size>stream_bridge::HevcCoverageHeaderBytes+stream_bridge::MaximumHevcCoverageBitstreamBytes||!pixels||pixels>MaximumPixels)return false;
        std::lock_guard<std::mutex> lock(impl_->gate);if(!impl_->prepared||impl_->stopping||impl_->nextTicket==UINT64_MAX)return false;
        for(auto& slot:impl_->slots)if(slot.phase==Impl::Phase::Free){
            // Publish only after this bounded copy succeeds; never retain the
            // caller's packet memory while a worker decodes asynchronously.
            slot.encoded.assign(input.payload.data,input.payload.data+input.payload.size);
            slot.input=input;slot.input.payload={slot.encoded.data(),slot.encoded.size()};slot.epoch=impl_->epoch;slot.ticket=++impl_->nextTicket;
            slot.phase=Impl::Phase::Queued;impl_->available.notify_one();return true;
        }
    }catch(...){}
    return false;
}
bool Pool::take(Result& output)noexcept{
    std::lock_guard<std::mutex> lock(impl_->gate);
    for(auto& slot:impl_->slots)if(slot.phase==Impl::Phase::Done){output=std::move(slot.result);slot={};return true;}
    return false;
}
void Pool::cancel()noexcept{
    std::lock_guard<std::mutex> lock(impl_->gate);
    if(impl_->epoch==UINT64_MAX){impl_->prepared=false;impl_->stopping=true;impl_->available.notify_all();}
    else ++impl_->epoch;
    for(auto& slot:impl_->slots)if(slot.phase==Impl::Phase::Queued||slot.phase==Impl::Phase::Done)slot={};
}
void Pool::stopCold()noexcept{impl_->stop();}
Statistics Pool::statistics()const noexcept{
    std::lock_guard<std::mutex> lock(impl_->gate);Statistics result;
    for(const auto& value:impl_->measurements){result.contextsOpened+=value.contextsOpened;result.attempts+=value.attempts;result.decoded+=value.decoded;result.totalMicros+=value.totalMicros;result.maxMicros=std::max(result.maxMicros,value.maxMicros);}
    return result;
}
} // namespace stream_bridge_mask_hevc
