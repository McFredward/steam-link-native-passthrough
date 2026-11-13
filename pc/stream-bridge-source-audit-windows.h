#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "stream-bridge-source-audit.h"
#include <memory>

namespace stream_bridge::sender {
// Dev diagnostics only. No format, frame, mask or camera authority is added.
class SourceAuditServer {
public:
    using VerifyFile=bool(*)(const wchar_t*,HANDLE);
    using Inventory=bool(*)(std::uint64_t,SourceAuditInventory&,void*);
    SourceAuditServer();~SourceAuditServer();
    void tick(VerifyFile,Inventory=nullptr,void* opaque=nullptr)noexcept;
    SourceAuditStatus status()const noexcept;
    void stop()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge::sender

// The observer has no OpenVR provider/control listener. It forwards native
// graphics calls and reports bounded scalar ancestry to the existing sender.
extern "C" DWORD WINAPI SFNP_CompositorSourceAuditStartV1(void*)noexcept;
