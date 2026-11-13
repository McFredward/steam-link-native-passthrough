#pragma once
#include "stream-bridge-source-audit-windows.h"
#include <vector>
#include <string>
#include <cwchar>

namespace stream_bridge::sender::source_audit {
struct ModuleSearch {
    std::uintptr_t base=0;std::size_t bytes=0;
    DWORD error=ERROR_MOD_NOT_FOUND;unsigned entries=0,matches=0;
};
inline std::wstring moduleSpelling(const wchar_t* input){
    std::wstring out=input;
    for(auto& ch:out)if(ch==L'/')ch=L'\\';
    if(!_wcsnicmp(out.c_str(),L"\\\\?\\UNC\\",8))out=L"\\\\"+out.substr(8);
    else if(out.compare(0,4,L"\\\\?\\")==0)out.erase(0,4);
    return out;
}
inline bool sameModuleFile(HANDLE expected,const wchar_t* candidate)noexcept {
    if(!expected||expected==INVALID_HANDLE_VALUE)return false;
    const auto other=CreateFileW(candidate,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(other==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION a{},b{};
    const bool okay=GetFileInformationByHandle(expected,&a)&&GetFileInformationByHandle(other,&b)&&
        a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow;
    CloseHandle(other);return okay;
}
// Public loader metadata only. Never use the truncated exit value of a remote
// LoadLibrary call as a Win64 module address. Caller retains its source file.
inline ModuleSearch findModule(DWORD pid,const std::wstring& expected,
    const SourceAuditDiscovery& api,HANDLE retained=nullptr){
    ModuleSearch result{};
    const auto snapshot=api.snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE){result.error=GetLastError();return result;}
    struct CloseSnapshot {HANDLE value;~CloseSnapshot(){CloseHandle(value);}} close{snapshot};
    std::vector<MODULEENTRY32W> entries;entries.reserve(128);unsigned count=0;
    MODULEENTRY32W entry{};entry.dwSize=sizeof entry;
    bool okay=api.first(snapshot,&entry)!=FALSE;
    DWORD error=okay?ERROR_SUCCESS:GetLastError();
    while(okay){
        if(count==1024){error=ERROR_MORE_DATA;break;}
        entries.push_back(entry);++count;okay=api.next(snapshot,&entry)!=FALSE;
        if(!okay)error=GetLastError();
    }
    result.entries=count;
    if(error!=ERROR_NO_MORE_FILES){result.error=error?error:ERROR_INVALID_DATA;return result;}
    const auto wanted=moduleSpelling(expected.c_str());
    const auto select=[&](const MODULEENTRY32W& value){
        const auto base=reinterpret_cast<std::uintptr_t>(value.modBaseAddr);
        if(result.matches&&result.base==base)return;
        ++result.matches;result.base=base;result.bytes=value.modBaseSize;
    };
    for(unsigned i=0;i<count;++i)if(!_wcsicmp(moduleSpelling(entries[i].szExePath).c_str(),wanted.c_str()))select(entries[i]);
    // File identity handles alternate directory/short-name/hardlink spellings.
    // The usual exact spelling needs no per-module filesystem walk.
    if(!result.matches){
        HANDLE own=retained;
        if(!own)own=CreateFileW(expected.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(own&&own!=INVALID_HANDLE_VALUE)for(unsigned i=0;i<count;++i)if(sameModuleFile(own,entries[i].szExePath))select(entries[i]);
        if(!retained&&own&&own!=INVALID_HANDLE_VALUE)CloseHandle(own);
    }
    result.error=result.matches==1&&result.base&&result.bytes?ERROR_SUCCESS:
        result.matches>1?ERROR_DUP_NAME:ERROR_MOD_NOT_FOUND;
    if(result.error){result.base=0;result.bytes=0;}
    return result;
}
} // namespace stream_bridge::sender::source_audit
