#pragma once
#include "bridge/stream_bridge_frame.h"
#include <filesystem>
#include <memory>

namespace stream_bridge_mask_hevc {
// Codec facts only. The caller must authenticate the coverage certificate and
// retain the independent exact native image/eye/lease and camera grants.
struct Input {
    stream_bridge::VideoStreamContext source{};
    stream_bridge::FrameDescriptor descriptor{};
    stream_bridge::Nonce binding{};
    stream_bridge::Bytes payload{};
};
struct DecodedMask {
    stream_bridge::VideoStreamContext source{};
    stream_bridge::FrameDescriptor descriptor{};
    stream_bridge::Nonce binding{};
    std::vector<std::uint8_t> samples;
    stream_bridge::CoverageRange range{};
};
struct Libraries {std::filesystem::path codec,utility;};
enum class Status {Ok,Unavailable,InvalidIdentity,InvalidEnvelope,DecoderRefused,
    InvalidOutput,WrongFrame,ExtraOutput,AllocationRefused,MissingReference};
const char* statusName(Status)noexcept;
struct Statistics {
    std::uint64_t contextsOpened=0,attempts=0,decoded=0,totalMicros=0,maxMicros=0,temporalDecoded=0,missingReferences=0;
};
// Serialized cold-worker owner only. No file/device I/O, library loading or
// decoding belongs in a renderer callback. Reuse one bounded decoder and its
// allocation/thread pools. Flush references between independent complete IDRs;
// fresh packet tokens and COPY_OPAQUE still prove every exact returned frame.
class Decoder {
public:
    Decoder();~Decoder();
    Decoder(const Decoder&)=delete;Decoder& operator=(const Decoder&)=delete;
    bool prepareCold(const Libraries&)noexcept; // Exact explicit public ABI.
    bool prepareInstalledCold()noexcept; // Only owned adjacent *-sfnp-mask libs.
    bool ready()const noexcept;
    Status decodeIntra(const Input&,DecodedMask&)noexcept; // Transactional.
    Status decodeTemporal(const Input&,DecodedMask&)noexcept;
    void invalidateTemporal()noexcept; // Serialized owner; revoke DPB after a known gap.
    Statistics statistics()const noexcept; // Serialized owner only, diagnostic.
    void stopCold()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge_mask_hevc
