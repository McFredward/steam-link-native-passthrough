#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace stream_bridge::sender {
constexpr std::size_t RgbOutputLogBytes=1024;
// Diagnostic scalars captured with the input, never hardware-effective rates,
// source permission or frame/image proof. Keep unknown/overlap explicit.
struct NvencQosObservation {
    std::uint64_t revision=0;
    std::uint32_t frameRateNum=0,frameRateDen=0;
    std::uint32_t requestedAverageBitRate=0,requestedMaximumBitRate=0;
    std::uint32_t requestedVbvBufferSize=0,requestedVbvInitialDelay=0;
    std::uint32_t appliedAverageBitRate=0,appliedMaximumBitRate=0;
    std::uint32_t appliedVbvBufferSize=0,appliedVbvInitialDelay=0;
    std::uint32_t reservedMetadataBitRate=0,meanMetadataBitRate=0;
    std::uint32_t rateControlMode=0;
    bool known=false,budgetApplied=false,encodeOverlappedReconfigure=false;
    bool resetEncoder=false,forceIDR=false;
};
struct RgbOutputSample {
    std::uint64_t observedUs=0,serial=0,encoderGeneration=0,submission=0,inputTimestamp=0;
    std::uint32_t frameIndex=0,pictureType=0,hardwareStatus=0,qp=0;
    std::uint32_t rgbBytes=0,completeBytes=0,width=0,height=0;
    bool exactInput=false,sessionAuthorized=false;
    NvencQosObservation qos{};
};
struct RgbOutputEvent {RgbOutputSample sample{};unsigned roles=0;};
struct RgbOutputReport {
    std::uint64_t beginUs=0,endUs=0,frames=0,unmatched=0,knownQos=0,overlap=0,sessionAuthorized=0,rejected=0;
    std::uint64_t rgbBytes=0,addedBytes=0,qpSamples=0,qpSum=0;
    std::uint64_t lowBudgetFrames=0,highQpFrames=0,lowBudgetHighQpFrames=0;
    std::uint32_t maxRgbBytes=0,maxAddedBytes=0,minQp=51,maxQp=0;
    std::array<RgbOutputEvent,5> events{};unsigned eventCount=0;
};
// Caller serializes with the EXISTING native metadata mutex. observe() copies
// scalars only: fixed storage, no clocks, allocation, I/O, waiting or control.
class RgbOutputTelemetry {
public:
    static constexpr unsigned LowRgb=1,HighQp=2,PeakRgb=4,PeakAdded=8,Last=16;
    static constexpr std::uint32_t MaximumBytes=64u*1024u*1024u;
    bool enabled()const noexcept{return enabled_;}
    void enable(bool value,std::uint64_t now)noexcept{
        if(enabled_==value)return;
        enabled_=value;window_={};window_.beginUs=now;selected_={};valid_={};lastObservedUs_=0;
    }
    void observe(RgbOutputSample sample)noexcept{
        if(!enabled_)return;
        if(!sample.observedUs||sample.observedUs<std::max(window_.beginUs,lastObservedUs_)||!sample.rgbBytes||sample.rgbBytes>MaximumBytes
           ||sample.completeBytes<sample.rgbBytes||sample.completeBytes>MaximumBytes){add(window_.rejected,1);return;}
        if(!sample.exactInput)sample.qos={}; // Never borrow a latest config for an unmatched output.
        lastObservedUs_=sample.observedUs;add(serial_,1);sample.serial=serial_;add(window_.frames,1);
        if(!sample.exactInput)add(window_.unmatched,1);
        if(sample.qos.known)add(window_.knownQos,1);
        if(sample.qos.encodeOverlappedReconfigure)add(window_.overlap,1);
        if(sample.sessionAuthorized)add(window_.sessionAuthorized,1);
        const bool lowBudget=sample.qos.known&&sample.qos.appliedAverageBitRate&&sample.qos.appliedAverageBitRate<=10000000;
        const bool highQp=sample.qp>=40&&sample.qp<=51;
        if(lowBudget)add(window_.lowBudgetFrames,1);
        if(highQp)add(window_.highQpFrames,1);
        if(lowBudget&&highQp)add(window_.lowBudgetHighQpFrames,1);
        const auto extra=sample.completeBytes-sample.rgbBytes;
        add(window_.rgbBytes,sample.rgbBytes);add(window_.addedBytes,extra);
        window_.maxRgbBytes=std::max(window_.maxRgbBytes,sample.rgbBytes);
        window_.maxAddedBytes=std::max(window_.maxAddedBytes,extra);
        if(sample.qp<=51){add(window_.qpSamples,1);add(window_.qpSum,sample.qp);
            window_.minQp=std::min(window_.minQp,sample.qp);window_.maxQp=std::max(window_.maxQp,sample.qp);
            if(!valid_[1]||sample.qp>selected_[1].qp)select(1,sample);
        }
        if(sample.qos.known&&sample.qos.appliedAverageBitRate
           &&(!valid_[0]||sample.qos.appliedAverageBitRate<selected_[0].qos.appliedAverageBitRate))select(0,sample);
        if(!valid_[2]||sample.rgbBytes>selected_[2].rgbBytes)select(2,sample);
        if(!valid_[3]||extra>selected_[3].completeBytes-selected_[3].rgbBytes)select(3,sample);
        select(4,sample);
    }
    RgbOutputReport take(std::uint64_t now)noexcept{
        if(!enabled_||now<std::max(window_.beginUs,lastObservedUs_))return {};
        auto report=window_;report.endUs=now;
        for(unsigned i=0;i<selected_.size();++i)if(valid_[i]){
            unsigned j=0;for(;j<report.eventCount;++j)if(report.events[j].sample.serial==selected_[i].serial)break;
            if(j==report.eventCount){report.events[j].sample=selected_[i];++report.eventCount;}
            report.events[j].roles|=1u<<i;
        }
        if(!report.qpSamples)report.minQp=0;
        window_={};window_.beginUs=now;selected_={};valid_={};return report;
    }
private:
    static void add(std::uint64_t& value,std::uint64_t increment)noexcept{
        value+=std::min(increment,std::numeric_limits<std::uint64_t>::max()-value);
    }
    void select(unsigned i,const RgbOutputSample& sample)noexcept{selected_[i]=sample;valid_[i]=true;}
    bool enabled_=false;std::uint64_t serial_=0,lastObservedUs_=0;RgbOutputReport window_{};
    std::array<RgbOutputSample,5> selected_{};std::array<bool,5> valid_{};
};
// Cold formatting only. Successful Lock sizes are encoder output, not measured
// network traffic. observed age uses a local monotonic clock; input PTS and
// native frameIdx are opaque and are NOT asserted equal to Frame FEC indices.
inline bool formatRgbOutputSummary(char* out,std::size_t size,const RgbOutputReport& r)noexcept{
    const auto us=r.endUs>=r.beginUs?r.endUs-r.beginUs:0;
    const auto rate=[&](std::uint64_t bytes){return us?double(bytes)*8.0/double(us):0.0;};
    const int n=std::snprintf(out,size,"RGB output interval: frames/unmatched/known-qos/overlap/session-auth/rejected=%llu/%llu/%llu/%llu/%llu/%llu; elapsed-us=%llu; bytes rgb/added=%llu/%llu, peaks=%u/%u; output-Mbit/s rgb/added=%.3f/%.3f; QP min/mean/max=%u/%.2f/%u, valid=%llu; frames passed<=10M/QP>=40/both=%llu/%llu/%llu; native Lock bytes, not link capacity or quality proof",
        static_cast<unsigned long long>(r.frames),static_cast<unsigned long long>(r.unmatched),static_cast<unsigned long long>(r.knownQos),
        static_cast<unsigned long long>(r.overlap),static_cast<unsigned long long>(r.sessionAuthorized),static_cast<unsigned long long>(r.rejected),
        static_cast<unsigned long long>(us),static_cast<unsigned long long>(r.rgbBytes),static_cast<unsigned long long>(r.addedBytes),
        r.maxRgbBytes,r.maxAddedBytes,rate(r.rgbBytes),rate(r.addedBytes),r.minQp,r.qpSamples?double(r.qpSum)/double(r.qpSamples):0.0,r.maxQp,static_cast<unsigned long long>(r.qpSamples),
        static_cast<unsigned long long>(r.lowBudgetFrames),static_cast<unsigned long long>(r.highQpFrames),static_cast<unsigned long long>(r.lowBudgetHighQpFrames));
    return n>=0&&std::size_t(n)<size;
}
inline bool formatRgbOutputEvent(char* out,std::size_t size,const RgbOutputReport& r,const RgbOutputEvent& e)noexcept{
    const auto& s=e.sample;const auto& q=s.qos;
    const unsigned flags=(s.exactInput?1u:0u)|(q.known?2u:0u)|(q.budgetApplied?4u:0u)|(q.encodeOverlappedReconfigure?8u:0u)|(s.sessionAuthorized?16u:0u);
    const int n=std::snprintf(out,size,"RGB output event: roles=0x%x; age-us=%llu; gen/submission=%llu/%llu; native-frame/pts=%u/%llu; extent=%ux%u; picture/hw/QP=%u/%u/%u; bytes rgb/added/complete=%u/%u/%u; flags=0x%x; qos-rev=%llu; fps=%u/%u, rc=%u, reset/IDR=%u/%u; avg requested/passed=%u/%u, max=%u/%u bit/s; VBV=%u/%u, delay=%u/%u bits; metadata reserved/mean=%u/%u bit/s",
        e.roles,static_cast<unsigned long long>(r.endUs>=s.observedUs?r.endUs-s.observedUs:0),static_cast<unsigned long long>(s.encoderGeneration),static_cast<unsigned long long>(s.submission),
        s.frameIndex,static_cast<unsigned long long>(s.inputTimestamp),s.width,s.height,s.pictureType,s.hardwareStatus,s.qp,s.rgbBytes,s.completeBytes-s.rgbBytes,s.completeBytes,flags,
        static_cast<unsigned long long>(q.revision),q.frameRateNum,q.frameRateDen,q.rateControlMode,unsigned(q.resetEncoder),unsigned(q.forceIDR),
        q.requestedAverageBitRate,q.appliedAverageBitRate,q.requestedMaximumBitRate,q.appliedMaximumBitRate,
        q.requestedVbvBufferSize,q.appliedVbvBufferSize,q.requestedVbvInitialDelay,q.appliedVbvInitialDelay,q.reservedMetadataBitRate,q.meanMetadataBitRate);
    return n>=0&&std::size_t(n)<size;
}
} // namespace stream_bridge::sender
