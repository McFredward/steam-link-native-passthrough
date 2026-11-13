#pragma once
#include "stream-bridge-source-audit.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <type_traits>

namespace stream_bridge::sender::source_audit {
constexpr std::uint64_t Magic=0x3154494455414653ull;
struct Launch {std::uint64_t magic=Magic,serverBirth=0;std::uint32_t server=0,reserved=0;};
// Each writer owns a separate seqlock. These are untrusted diagnostics, never
// a renderer input or authentication mechanism. The current-user DACL prevents
// accidental cross-user disclosure of private allocation identities.
struct Page {
    std::uint64_t magic=Magic;std::uint32_t server=0,bytes=sizeof(Page);
    volatile LONG stopped=0;
    alignas(8) volatile LONG64 inventorySequence=0;
    SourceAuditInventory inventory{};
    alignas(8) volatile LONG64 statusSequence=0;
    SourceAuditStatus status{};
};
static_assert(std::is_trivially_copyable<SourceAuditInventory>::value&&
              std::is_trivially_copyable<SourceAuditStatus>::value,"fixed diagnostic transport");
static_assert(sizeof(Page)<=65536,"bounded local diagnostics");
inline bool name(DWORD pid,wchar_t(&value)[96])noexcept {
    return pid&&std::swprintf(value,96,L"Local\\SFNP.SourceAudit.V1.%lu",static_cast<unsigned long>(pid))>0;
}
inline std::uint64_t birth(HANDLE process)noexcept {
    FILETIME created{},exit{},kernel{},user{};
    return GetProcessTimes(process,&created,&exit,&kernel,&user)?
        (std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime:0;
}
template<class T>void publish(volatile LONG64& sequence,T& target,const T& source)noexcept {
    InterlockedIncrement64(&sequence);MemoryBarrier();
    std::memcpy(&target,&source,sizeof(T));MemoryBarrier();InterlockedIncrement64(&sequence);
}
template<class T>bool read(volatile LONG64& sequence,const T& source,T& target)noexcept {
    const auto before=InterlockedCompareExchange64(&sequence,0,0);
    if(before&1)return false;
    std::memcpy(&target,&source,sizeof(T));MemoryBarrier();
    return before==InterlockedCompareExchange64(&sequence,0,0);
}
} // namespace stream_bridge::sender::source_audit
