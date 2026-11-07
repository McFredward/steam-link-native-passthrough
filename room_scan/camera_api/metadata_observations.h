// Bounded diagnostic metadata history. No images or room-scan authority.
#pragma once
#include "linux_inventory.h"
#include <deque>

namespace room_camera_api {
class MetadataObservations {
    struct Entry {std::uint64_t token,observed;Json metadata;};
    std::deque<Entry> entries_;
    std::uint64_t serial_=0;
    Json identity_;
public:
    void add(Json metadata,std::uint64_t now){
        if(!metadata.is_object()||metadata.dump().size()>12288)
            throw std::runtime_error("Diagnostic metadata exceeds budget");
        if(metadata.value("schema","")!="sfnp-native-camera-metadata-v1"
            ||metadata.value("contains_image_data",true)||metadata.value("room_scan_ready",true)
            ||metadata.contains("pixels")||metadata.contains("images"))
            throw std::runtime_error("Only diagnostic metadata can enter its history");
        const Json identity={metadata.at("source_session"),metadata.at("producer_pid"),metadata.at("producer_start_ticks")};
        if(!identity_.is_null()&&identity_!=identity)
            throw std::runtime_error("Diagnostic metadata producer changed");
        if(!now||(!entries_.empty()&&now<entries_.back().observed))
            throw std::runtime_error("Diagnostic metadata clock moved backwards");
        identity_=identity;entries_.push_back({++serial_,now,std::move(metadata)});
        if(entries_.size()>32)entries_.pop_front();
    }
    Json next(std::uint64_t after,std::uint64_t now)const{
        if(after>serial_)return {{"status","invalid-metadata-cursor"}};
        if(!entries_.empty()&&after<entries_.front().token-1)
            return {{"status","metadata-history-evicted"},{"resume_after",entries_.front().token-1}};
        for(const auto& entry:entries_)if(entry.token>after){
            if(now<entry.observed||now-entry.observed>=500000000u)
                return {{"status","no-fresh-metadata"},{"resume_after",entry.token}};
            return {{"schema","sfnp-native-camera-metadata-observation-v1"},
                    {"status","metadata"},{"observation_token",entry.token},
                    {"observed_monotonic_ns",entry.observed},{"metadata",entry.metadata},
                    {"contains_image_data",false},{"room_scan_ready",false}};
        }
        return {{"status","no-fresh-metadata"}};
    }
};
} // namespace room_camera_api
