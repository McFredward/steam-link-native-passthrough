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
struct HevcCoverageView {
    Bytes annexB{};
    std::uint16_t width=0,height=0;
};
// Transactional; borrowed annexB remains valid only as long as input. Requires
// self-contained VPS/SPS/PPS and exactly one first-slice IDR (19 or20), layer0,
// temporal0, Main8bit420, no cropping, reordering or inter-frame references.
bool inspectHevcCoverage(Bytes payload,std::uint16_t expectedWidth,
    std::uint16_t expectedHeight,HevcCoverageView&) noexcept;
bool wrapHevcCoverage(Bytes annexB,std::uint16_t width,std::uint16_t height,
    std::vector<std::uint8_t>& output) noexcept;
} // namespace stream_bridge
