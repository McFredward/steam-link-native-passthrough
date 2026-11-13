// Owned DLL and controlled public-metadata faults; never a Valve image.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pc/stream-bridge-source-audit-windows.h"
#include <cstdio>
#include <cwchar>
#include <string>
namespace {
bool allowed=false,discoveryActive=false,duplicatePending=false;unsigned verifications=0,control=0,transient=0;
std::wstring aliasPath,foreignPath;MODULEENTRY32W duplicate{};
bool positive(unsigned value){return value==1||value==4||value==6||value==7;}
bool verify(const wchar_t*,HANDLE file){++verifications;LARGE_INTEGER bytes{};return allowed&&GetFileSizeEx(file,&bytes)&&bytes.QuadPart>=512;}
HANDLE WINAPI snapshot(DWORD flags,DWORD pid){
    if(flags&(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32)){
        duplicatePending=false;
        if(discoveryActive&&(control==5||(control==4&&transient++<4))){SetLastError(ERROR_BAD_LENGTH);return INVALID_HANDLE_VALUE;}
    }
    return CreateToolhelp32Snapshot(flags,pid);
}
void rewrite(MODULEENTRY32W& entry){
    if(!discoveryActive||_wcsicmp(entry.szModule,L"audit_fixture.dll"))return;
    std::wstring path=entry.szExePath;
    if(control==6&&path.compare(0,4,L"\\\\?\\")!=0)path=L"\\\\?\\"+path;
    if(control==7)path=aliasPath;
    if(control==8)path=foreignPath;
    if(path.size()<MAX_PATH)std::wmemcpy(entry.szExePath,path.c_str(),path.size()+1);
    if(control==9){duplicate=entry;duplicate.modBaseAddr=reinterpret_cast<BYTE*>(reinterpret_cast<std::uintptr_t>(entry.modBaseAddr)+65536);duplicatePending=true;}
}
BOOL WINAPI first(HANDLE handle,LPMODULEENTRY32W entry){const auto okay=Module32FirstW(handle,entry);if(okay)rewrite(*entry);return okay;}
BOOL WINAPI next(HANDLE handle,LPMODULEENTRY32W entry){
    if(duplicatePending){duplicatePending=false;*entry=duplicate;return TRUE;}
    const auto okay=Module32NextW(handle,entry);if(okay)rewrite(*entry);return okay;
}
}
extern "C" __declspec(dllexport) BOOL WINAPI AuditFixtureRun(DWORD expected){
    using namespace stream_bridge::sender;
    control=expected;allowed=expected!=2;wchar_t path[32768]{};
    if(!GetModuleFileNameW(nullptr,path,32768))return FALSE;
    const std::wstring exe=path;const auto slash=exe.find_last_of(L"\\/");if(slash==std::wstring::npos)return FALSE;
    const auto directory=exe.substr(0,slash+1);aliasPath=directory+L"alias\\audit_alias.dll";
    if(expected==7){
        CreateDirectoryW((directory+L"alias").c_str(),nullptr);
        if(!CreateHardLinkW(aliasPath.c_str(),(directory+L"audit_fixture.dll").c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)return FALSE;
    }
    const auto system=GetSystemDirectoryW(path,32768);if(!system||system>=32768)return FALSE;foreignPath=std::wstring(path)+L"\\kernel32.dll";
    SourceAuditServer server(SourceAuditDiscovery{snapshot,first,next});
    const auto deadline=GetTickCount64()+12000;SourceAuditStatus state{};bool pass=false;
    do{
        discoveryActive=server.status().state==2;server.tick(verify);state=server.status();
        if(positive(expected)&&state.devices&&state.draws&&state.state>=5&&state.state<8){pass=true;break;}
        if(expected==2&&state.state==8&&state.error==ERROR_INVALID_EXE_SIGNATURE){pass=true;break;}
        if(expected==3&&GetTickCount64()+10000>=deadline){pass=!state.pid&&state.state==1&&!verifications;break;}
        if(expected==5&&state.state==8&&state.error==ERROR_BAD_LENGTH&&state.failureStage==5){pass=true;break;}
        if(expected==8&&state.state==8&&state.error==ERROR_MOD_NOT_FOUND&&state.failureStage==5){pass=true;break;}
        if(expected==9&&state.state==8&&state.error==ERROR_DUP_NAME&&state.failureStage==5){pass=true;break;}
        Sleep(10);
    }while(GetTickCount64()<deadline);
    if(positive(expected))pass=pass&&verifications==1&&state.loadCalls==1&&state.entryCalls==1;
    if(expected==4)pass=pass&&state.discoveryMisses==4;
    if(expected==2)pass=pass&&verifications==1&&!state.pid&&!state.loadCalls&&!state.entryCalls;
    if(expected==5||expected==8||expected==9)pass=pass&&state.loadCalls==1&&!state.entryCalls;
    server.stop();
    std::printf("owned loader control=%lu: %s; pid=%u state=%u error=%u stage=%u remote-result=%u; discovery error/entries/matches/misses=%u/%u/%u/%u, load/entry=%u/%u; devices=%llu draws=%llu verifier=%u\n",
        static_cast<unsigned long>(expected),pass?"PASS":"FAIL",state.pid,state.state,state.error,state.failureStage,state.remoteResult,
        state.discoveryError,state.discoveryEntries,state.discoveryMatches,state.discoveryMisses,state.loadCalls,state.entryCalls,
        static_cast<unsigned long long>(state.devices),static_cast<unsigned long long>(state.draws),verifications);
    return pass?TRUE:FALSE;
}
