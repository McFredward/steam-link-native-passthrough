#pragma once
#include "stream_bridge_protocol.h"
#include <vector>

namespace stream_bridge {
// Encoding6 is independently decodable, lossy HEVC Main 8-bit 4:2:0. Only the
// decoded full-range Y plane is coverage; neutral chroma is never sampled.
// This portable parser establishes bounded syntax/profile/extent, not successful
// decoding, current-image ownership or per-eye/source association.
constexpr std::size_t HevcCoverageHeaderBytes=32;
constexpr std::size_t MaximumHevcCoverageBitstreamBytes=2u*1024u*1024u;
constexpr unsigned MaximumHevcCoverageSlices=4;
struct HevcCoverageView {
    Bytes annexB{};
    std::uint16_t width=0,height=0;
    bool temporal=false,keyframe=true;
    std::uint64_t epoch=0,sequence=0,dependency=0,anchor=0;
    unsigned poc=0,pocBits=0;
};
// Transactional; borrowed annexB remains valid only as long as input. Requires
// self-contained VPS/SPS/PPS and exactly one IDR picture (19 or20), layer0,
// temporal0, Main8bit420, no cropping, reordering or inter-frame references.
// Up to four independent I slices must have increasing in-picture CTB addresses;
// bounded explicit unused SPS reference sets do not grant inter-frame decoding.
bool inspectHevcCoverage(Bytes payload,std::uint16_t expectedWidth,
    std::uint16_t expectedHeight,HevcCoverageView&) noexcept;
bool wrapHevcCoverage(Bytes annexB,std::uint16_t width,std::uint16_t height,
    std::vector<std::uint8_t>& output) noexcept;
// Codec7 keeps the same Main8/full-range image contract, but permits only a
// forward IP chain with one active L0 reference (previous POC), no B,
// reordering or long-term references. Every AU repeats its parameter sets.
// Epoch/sequence/dependency are authenticated codec state, never image authority.
constexpr std::size_t HevcTemporalHeaderBytes=64;
constexpr unsigned HevcTemporalGop=8;
bool inspectHevcTemporalCoverage(Bytes,std::uint16_t,std::uint16_t,HevcCoverageView&) noexcept;
bool wrapHevcTemporalCoverage(Bytes,std::uint16_t,std::uint16_t,std::uint64_t epoch,
    std::uint64_t sequence,std::uint64_t dependency,bool keyframe,std::uint64_t anchor,std::vector<std::uint8_t>&) noexcept;
} // namespace stream_bridge
