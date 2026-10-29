#pragma once
// Private diagnostics only. These unsigned records never establish source,
// video, camera, frame, eye or composition authority.
#include "bridge/stream_bridge_frame.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>

namespace stream_bridge::sender::capture {
constexpr std::size_t HeaderBytes=48,RecordBytes=176,FooterBytes=32;
constexpr std::size_t MaximumPayload=8u*1024u*1024u,MaximumBytes=48u*1024u*1024u;
constexpr std::uint32_t MaximumFrames=128;
using Token=std::array<std::uint8_t,16>;
enum class Stop:std::uint32_t {Capturing=0,Complete=1,ByteLimit=2,QueueBusy=3,QueueFull=4,
    SourceChanged=5,FrameGap=6,Timeout=7,Stopped=8,InvalidSample=9,IoError=10};
struct Metadata {
    std::uint64_t frame=0,encoderGeneration=0,sourceContract=0,timestampUs=0;
    std::uint32_t pid=0,originalVideoBytes=0,completeAuBytes=0,encodeUs=0;
    std::uint16_t width=0,height=0;
    CoverageEncoding encoding=CoverageEncoding::None;
    std::uint64_t nativeInputTimestamp=0; // Opaque native units, not a guessed time conversion.
    // Last successful forwarding observed at Encode, not hardware config proof.
    std::uint32_t qosFlags=0; // known, budgetApplied, reconfigure overlap before matching Lock
    std::uint64_t qosRevision=0;
    std::uint32_t frameRateNum=0,frameRateDen=0;
    std::uint32_t requestedAverageBitRate=0,requestedMaximumBitRate=0,requestedVbvBufferSize=0,requestedVbvInitialDelay=0;
    std::uint32_t appliedAverageBitRate=0,appliedMaximumBitRate=0,appliedVbvBufferSize=0,appliedVbvInitialDelay=0;
    std::uint32_t reservedMetadataBitRate=0,meanMetadataBitRate=0;
};
struct Sample {Metadata metadata{};std::vector<std::uint8_t> payload;};
bool valid(const Metadata&,std::size_t payloadBytes)noexcept;
std::array<std::uint8_t,HeaderBytes> header(const Token&)noexcept;
bool parseHeader(Bytes,Token&)noexcept;
std::array<std::uint8_t,RecordBytes> recordHeader(const Sample&);
bool parseRecordHeader(Bytes,Metadata&,std::uint32_t& payloadBytes,std::array<std::uint8_t,32>& digest)noexcept;
bool verifyRecord(Bytes header,Bytes payload)noexcept;
std::array<std::uint8_t,FooterBytes> footer(Stop,std::uint32_t frames,std::uint64_t bytes,std::uint64_t lastFrame)noexcept;
bool parseFooter(Bytes,Stop&,std::uint32_t&,std::uint64_t&,std::uint64_t&)noexcept;
const char* stopName(Stop)noexcept;

// Producer never waits and moves an already encoded, no-longer-needed vector.
// A diagnostic drop ends capture; the game output is entirely unaffected.
class Queue {
public:
    void start(); // Cold, previous writer joined.
    void offer(const Metadata&,std::vector<std::uint8_t>&&)noexcept;
    bool pop(Sample&);
    void discard(); // Cold writer only, after stopping.
    void stop(Stop)noexcept;
    void wait(); // Writer only, bounded.
    bool active()const noexcept{return reason_.load(std::memory_order_acquire)==Stop::Capturing;}
    Stop reason()const noexcept{return reason_.load(std::memory_order_acquire);}
private:
    std::mutex mutex_;std::condition_variable changed_;
    std::array<Sample,4> samples_{};std::size_t read_=0,count_=0;
    std::uint32_t frames_=0;std::size_t bytes_=HeaderBytes+FooterBytes;Metadata last_{};
    std::atomic<Stop> reason_{Stop::Stopped};
    std::atomic<std::uint64_t> epoch_{0};
};
class Writer {
public:
    Writer();~Writer();
    Writer(const Writer&)=delete;Writer& operator=(const Writer&)=delete;
    void configure(const std::filesystem::path& privateRoot)noexcept; // Cold; no files created.
    void poll()noexcept; // Cold, internally limited to 1 Hz.
    void offer(const Metadata& m,std::vector<std::uint8_t>&& data)noexcept;
    bool active()const noexcept;
    void stop()noexcept; // Cold; joins writer. Never called from a native hook.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge::sender::capture
