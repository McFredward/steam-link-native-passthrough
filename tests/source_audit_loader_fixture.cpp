// Owned DLL used by the real production loader; never a Valve image.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pc/stream-bridge-source-audit-windows.h"
#include <cstdio>
namespace {
bool allowed=false;unsigned verifications=0;
bool verify(const wchar_t*,HANDLE file){
    ++verifications;LARGE_INTEGER bytes{};
    return allowed&&GetFileSizeEx(file,&bytes)&&bytes.QuadPart>=512;
}
}
extern "C" __declspec(dllexport) BOOL WINAPI AuditFixtureRun(DWORD expected){
    using namespace stream_bridge::sender;
    allowed=expected==1;SourceAuditServer server;
    const auto deadline=GetTickCount64()+10000;SourceAuditStatus state{};bool pass=false;
    do{
        server.tick(verify);state=server.status();
        if(expected==1&&state.devices&&state.draws&&state.state>=5&&state.state<8){pass=true;break;}
        if(expected==2&&state.state==8&&state.error==ERROR_INVALID_EXE_SIGNATURE){pass=true;break;}
        if(expected==3&&GetTickCount64()+8000>=deadline){pass=!state.pid&&state.state==1&&!verifications;break;}
        Sleep(10);
    }while(GetTickCount64()<deadline);
    if(expected==1)pass=pass&&verifications==1;
    if(expected==2)pass=pass&&verifications==1&&!state.pid;
    server.stop();
    std::printf("owned loader control=%lu: %s; state=%u error=%u devices=%llu draws=%llu verifier=%u\n",
        static_cast<unsigned long>(expected),pass?"PASS":"FAIL",state.state,state.error,
        static_cast<unsigned long long>(state.devices),static_cast<unsigned long long>(state.draws),verifications);
    return pass?TRUE:FALSE;
}
