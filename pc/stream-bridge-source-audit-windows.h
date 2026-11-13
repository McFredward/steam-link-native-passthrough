#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include "stream-bridge-source-audit.h"
#include <memory>

namespace stream_bridge::sender {
struct SourceAuditDiscovery {
    decltype(&CreateToolhelp32Snapshot) snapshot=&CreateToolhelp32Snapshot;
    decltype(&Module32FirstW) first=&Module32FirstW;
    decltype(&Module32NextW) next=&Module32NextW;
};
// Dev diagnostics only. No format, frame, mask or camera authority is added.
class SourceAuditServer {
public:
    using VerifyFile=bool(*)(const wchar_t*,HANDLE);
    using Inventory=bool(*)(std::uint64_t,SourceAuditInventory&,void*);
    explicit SourceAuditServer(SourceAuditDiscovery={});~SourceAuditServer();
    void tick(VerifyFile,Inventory=nullptr,void* opaque=nullptr)noexcept;
    SourceAuditStatus status()const noexcept;
    void stop()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace stream_bridge::sender

// The observer has no OpenVR provider/control listener. It forwards native
// graphics calls and reports bounded scalar ancestry to the existing sender.
extern "C" __declspec(dllexport) DWORD WINAPI SFNP_CompositorSourceAuditStartV1(void*)noexcept;
