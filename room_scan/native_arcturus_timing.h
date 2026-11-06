// EOF-anchored timing of the supported IMX616 NV12 mode. A completed, stable
// native group-hold history is required; a latest register alone is insufficient.
#pragma once
#include "native_camera.h"
#include <optional>
namespace room_native {
inline std::optional<float> arcturusSampleOffset(const Json& frame){
    if(frame.value("width",0)!=616||frame.value("height",0)!=616
        ||frame.value("source_pitch_bytes",0)!=3328||frame.value("source_height",0)!=2464)return std::nullopt;
    const auto encoding=frame.value("encoding","");
    if(encoding=="nv12-u8-luma-candidate"){
        if(frame.contains("sampling")&&frame.at("sampling")!=Json({{"step",4},{"area",{1,1}},{"center_offset",{0.,0.}}}))return std::nullopt;
        return 0.f;
    }
    if(encoding=="nv12-u8-area4-candidate"&&frame.contains("sampling")
        &&frame.at("sampling")==Json({{"step",4},{"area",{4,4}},{"center_offset",{1.5,1.5}}}))return 1.5f;
    if(encoding=="nv12-u8-area2-tile-candidate"&&frame.contains("sampling")&&frame.contains("tile")
        &&frame.at("sampling")==Json({{"step",2},{"area",{2,2}},{"center_offset",{.5,.5}}})){
        const auto& tile=frame.at("tile");
        if(!tile.is_object()||!tile.contains("index")||!tile.at("index").is_number_unsigned())return std::nullopt;
        const auto index=tile.at("index").get<unsigned>();
        if(index<4&&tile==Json({{"index",index},{"origin",{(index%2)*616,(index/2)*616}},{"full_extent",{1232,1232}}}))return .5f;
    }
    return std::nullopt;
}
struct ArcturusTiming {
    std::uint64_t firstRowCenter=0,lastRowCenter=0,center=0,integration=0,line=4360,uncertainty=0,minimumIntegration=0,maximumIntegration=0;
    std::uint64_t atRow(float row)const{
        if(!std::isfinite(row)||row<0||row>2463)throw std::runtime_error("Invalid exposure row");
        return firstRowCenter+std::uint64_t(std::llround(double(row)*line));
    }
};
inline std::optional<ArcturusTiming> arcturusTiming(const Json& frame){
    if(!arcturusSampleOffset(frame)||frame.value("width",0)!=616||frame.value("height",0)!=616
        ||frame.value("source_pitch_bytes",0)!=3328||frame.value("source_height",0)!=2464
        ||!frame.value("queue_mapping_exact",false)||!frame.value("copy_stable",false)||!frame.value("kernel_role_verified",false))return std::nullopt;
    if(!frame.contains("capture_metadata")||!frame["capture_metadata"].is_object()
        ||!frame.contains("observed_exposure_registers")||!frame["observed_exposure_registers"].is_object())return std::nullopt;
    const auto& capture=frame.at("capture_metadata");const auto& exposure=frame.at("observed_exposure_registers");
    if(capture.value("capture_clock","")!="CLOCK_MONOTONIC_RAW"||!capture.value("v4l_timestamp_matches",false)
        ||!exposure.value("successful_i2c_result",false))return std::nullopt;
    const auto sof=capture.at("sof_raw_ns").get<std::uint64_t>(),eof=capture.at("eof_raw_ns").get<std::uint64_t>();
    const auto previous=capture.at("previous_sof_raw_ns").get<std::uint64_t>();
    const auto stable=exposure.at("same_value_since_raw_ns").get<std::uint64_t>(),release=exposure.at("group_release_raw_ns").get<std::uint64_t>();
    if(!previous||previous>=sof||eof<sof||sof-previous>40000000u||sof-previous<14000000u)return std::nullopt;
    const auto period=sof-previous;
    // Two complete earlier frames must have the same successful setting, and
    // the latest complete group must precede this SOF and still be fresh.
    const bool exact=exposure.value("unchanged_through_eof",false)&&stable<sof&&sof-stable>=2*period
        &&release<sof&&sof-release<=2*period&&exposure.value("same_value_group_count",0)>=3;
    if(!exact&&!exposure.value("envelope_verified",false))return std::nullopt;
    ArcturusTiming out;constexpr std::uint64_t height=2464;
    // This mode has a 4.36 us native row period. SOF includes embedded/blanking
    // lines; EOF anchors the active rolling readout. Refuse another mode.
    const auto readout=height*out.line;
    if(eof-sof<readout||eof-sof>readout+128*out.line)return std::nullopt;
    const auto coarse=exposure.at("coarse_integration_lines").get<unsigned>();
    if(!coarse||coarse>65535)return std::nullopt;
    out.integration=std::uint64_t(coarse)*out.line;
    out.minimumIntegration=out.maximumIntegration=out.integration;
    if(!exact){
        const auto anchor=exposure.at("envelope_anchor_raw_ns").get<std::uint64_t>();
        const auto minimum=exposure.at("envelope_min_coarse").get<unsigned>(),maximum=exposure.at("envelope_max_coarse").get<unsigned>();
        if(!anchor||anchor>=sof||anchor>release||release>=sof||sof-release>2*period
            ||!minimum||maximum<minimum||maximum>65535||coarse<minimum||coarse>maximum)return std::nullopt;
        out.minimumIntegration=std::uint64_t(minimum)*out.line;out.maximumIntegration=std::uint64_t(maximum)*out.line;
        out.integration=out.minimumIntegration+(out.maximumIntegration-out.minimumIntegration)/2;
        // The row center is EOF-readout-integration/2: an integration interval
        // of width R gives a center interval of width R/2 and radius R/4.
        // Query latency and the entire center interval must still pass the
        // unchanged 0.5 mm / 0.5 mrad spatial gate before any image is exported.
        out.uncertainty=out.maximumIntegration==out.minimumIntegration?0:(out.maximumIntegration-out.minimumIntegration+3)/4+1;
        if(out.uncertainty>1500000u)return std::nullopt;
    }
    if(out.maximumIntegration+2*out.line>=period||eof<readout+out.maximumIntegration)return std::nullopt;
    out.firstRowCenter=eof-readout-out.integration/2;
    out.lastRowCenter=out.firstRowCenter+(height-1)*out.line;
    out.center=out.firstRowCenter+(height-1)*out.line/2;
    return out;
}
} // namespace room_native
