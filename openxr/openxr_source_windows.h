#pragma once
#include <cstdint>
#include <memory>

// The layer's SBN1 client. One worker owns the single live connection; the
// application threads only publish intent and read the current grant.
namespace stream_bridge::openxr {
// Whether the pipe server is the sender. The default accepts a server whose
// image lies in a SteamVR runtime directory listed in the user's
// %LOCALAPPDATA%\openvr\openvrpaths.vrpath ("runtime"), as the application
// source contract asks; tests substitute their own rule.
using TrustedServer=bool(*)(std::uint32_t pid);
bool steamVrServer(std::uint32_t pid)noexcept;
// A trusted server answers a request within waitMs (granting or not).
bool probeSource(unsigned waitMs,TrustedServer trusted=steamVrServer)noexcept;
struct SourceState {bool connected=false,granted=false,budget=false;std::uint64_t generation=0;};
class SourceClient {
public:
    explicit SourceClient(TrustedServer trusted=steamVrServer);~SourceClient();
    SourceClient(const SourceClient&)=delete;SourceClient& operator=(const SourceClient&)=delete;
    // Called at frame boundaries; a change wakes the worker, nothing blocks.
    void want(bool value)noexcept;
    SourceState state()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge::openxr
