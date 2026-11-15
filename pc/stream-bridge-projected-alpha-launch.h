#pragma once
#include "stream-bridge-source-audit-shared.h"
#include "stream-bridge-projected-alpha-channel.h"
namespace stream_bridge::sender::source_audit {
struct LaunchV2 {
    Launch diagnostic{};
    projected_alpha::ChannelLaunch production{};
};
}
