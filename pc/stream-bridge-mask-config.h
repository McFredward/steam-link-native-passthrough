#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace stream_bridge::sender {
// Read once by the cold provider. Missing configuration selects the default;
// explicit zero or unreadable/invalid configuration selects lossless masks.
// A rate target is not a wire-size guarantee. Negotiation is still mandatory.
inline std::uint32_t maskHevcBitrate(std::optional<std::string_view> configured)noexcept{
    if(!configured)return 20000000;
    auto value=*configured;
    constexpr std::string_view marker="SFNP_MASK_HEVC_V1\n";
    if(value.size()<=marker.size()+1||value.size()>64||value.substr(0,marker.size())!=marker||value.back()!='\n')return 0;
    value.remove_prefix(marker.size());value.remove_suffix(1);
    std::uint32_t rate=0;
    for(const auto c:value){if(c<'0'||c>'9'||rate>20000000)return 0;rate=rate*10+std::uint32_t(c-'0');}
    return rate>=1000000&&rate<=200000000?rate:0;
}
}
