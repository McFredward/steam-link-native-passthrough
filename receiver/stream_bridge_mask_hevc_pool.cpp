#include "stream_bridge_mask_hevc_pool.h"
#include "bridge/stream_bridge_hevc_coverage.h"
#include <array>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace stream_bridge_mask_hevc {
struct Pool::Impl {
    enum class Phase {Free,Queued,Running,Done};
    struct Chain {
        bool temporal=false;stream_bridge::VideoStreamContext source{};stream_bridge::FrameDescriptor descriptor{};
        stream_bridge::Nonce binding{};std::uint64_t codecEpoch=0,anchor=0,cancelEpoch=0;
        bool operator==(const Chain& b)const noexcept{
            return temporal&&b.temporal&&source.connection==b.source.connection&&source.generation==b.source.generation&&
                descriptor.encoderGeneration==b.descriptor.encoderGeneration&&descriptor.codedWidth==b.descriptor.codedWidth&&
                descriptor.codedHeight==b.descriptor.codedHeight&&binding==b.binding&&codecEpoch==b.codecEpoch&&anchor==b.anchor&&cancelEpoch==b.cancelEpoch;
        }
    };
    struct Slot {
        Phase phase=Phase::Free;std::uint64_t ticket=0,epoch=0;
        Input input{};Chain chain{};std::vector<std::uint8_t> encoded;Result result;
    };
    mutable std::mutex gate;std::condition_variable available;
    std::array<Slot,Capacity> slots{};std::array<std::thread,Workers> threads;
    std::array<std::unique_ptr<Decoder>,Workers*2> decoders;
    std::array<Statistics,Workers*2> measurements{};
    std::array<Chain,Workers> chains{};std::array<std::uint64_t,Workers> decoderEpoch{};bool prepared=false,stopping=true;std::uint64_t epoch=1,nextTicket=0;
    Wake wake=nullptr;void* context=nullptr;
    bool eligible(const Slot& slot,unsigned index)const noexcept{
        if(slot.phase!=Phase::Queued)return false;
        if(!slot.chain.temporal)return true;
        for(unsigned i=0;i<Workers;++i)if(chains[i]==slot.chain)return i==index;
        // A new independent group can use an idle context, but cannot evict
        // another group's queued/running reference chain. No global P bottleneck.
        for(const auto& pending:slots)if((pending.phase==Phase::Queued||pending.phase==Phase::Running)&&pending.chain==chains[index])return false;
        return true;
    }
    bool queued(unsigned index)const noexcept{return std::any_of(slots.begin(),slots.end(),[&](const auto& s){return eligible(s,index);});}
    void run(unsigned index)noexcept{
        for(;;){
            Slot* slot=nullptr;bool temporal=false,reset=false;
            {
                std::unique_lock<std::mutex> lock(gate);
                available.wait(lock,[&]{return stopping||queued(index);});
                if(stopping)return;
                for(auto& candidate:slots)if(eligible(candidate,index)&&(!slot||candidate.ticket<slot->ticket))slot=&candidate;
                slot->phase=Phase::Running;temporal=slot->chain.temporal;
                if(temporal){chains[index]=slot->chain;reset=decoderEpoch[index]!=slot->epoch;decoderEpoch[index]=slot->epoch;}
            }
            auto& result=slot->result;result.identity=slot->input;result.identity.payload={};
            const auto decoder=temporal?Workers+index:index;
            if(reset)decoders[decoder]->invalidateTemporal();
            result.status=temporal?decoders[decoder]->decodeTemporal(slot->input,result.decoded):decoders[decoder]->decodeIntra(slot->input,result.decoded);
            bool publish=false;
            {
                std::lock_guard<std::mutex> lock(gate);measurements[decoder]=decoders[decoder]->statistics();
                if(!stopping&&slot->epoch==epoch){slot->phase=Phase::Done;publish=true;}
                else *slot={};
            }
            available.notify_all();if(publish&&wake)wake(context);
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
        std::lock_guard<std::mutex> lock(gate);slots={};chains={};decoderEpoch={};wake=nullptr;context=nullptr;
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
        if(!input.payload.data||input.payload.size<stream_bridge::HevcCoverageHeaderBytes||input.payload.size>stream_bridge::HevcTemporalHeaderBytes+stream_bridge::MaximumHevcCoverageBitstreamBytes||!pixels||pixels>MaximumPixels)return false;
        Impl::Chain chain;
        chain.temporal=input.payload.size>4&&!std::memcmp(input.payload.data,"SFHT",4);
        if(chain.temporal){stream_bridge::HevcCoverageView view;
            if(!stream_bridge::inspectHevcTemporalCoverage(input.payload,input.descriptor.codedWidth,input.descriptor.codedHeight,view))return false;
            chain.source=input.source;chain.descriptor=input.descriptor;chain.binding=input.binding;chain.codecEpoch=view.epoch;chain.anchor=view.anchor;}
        std::lock_guard<std::mutex> lock(impl_->gate);if(!impl_->prepared||impl_->stopping||impl_->nextTicket==UINT64_MAX)return false;
        for(auto& slot:impl_->slots)if(slot.phase==Impl::Phase::Free){
            // Publish only after this bounded copy succeeds; never retain the
            // caller's packet memory while a worker decodes asynchronously.
            slot.encoded.assign(input.payload.data,input.payload.data+input.payload.size);
            slot.input=input;slot.input.payload={slot.encoded.data(),slot.encoded.size()};slot.epoch=impl_->epoch;slot.ticket=++impl_->nextTicket;
            chain.cancelEpoch=impl_->epoch;slot.chain=chain;slot.phase=Impl::Phase::Queued;impl_->available.notify_all();return true;
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
    for(const auto& value:impl_->measurements){result.contextsOpened+=value.contextsOpened;result.attempts+=value.attempts;result.decoded+=value.decoded;result.totalMicros+=value.totalMicros;result.maxMicros=std::max(result.maxMicros,value.maxMicros);result.temporalDecoded+=value.temporalDecoded;result.missingReferences+=value.missingReferences;}
    return result;
}
} // namespace stream_bridge_mask_hevc
