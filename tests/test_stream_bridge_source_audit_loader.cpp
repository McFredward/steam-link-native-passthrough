// Same owned EXE is built as vrserver.exe and vrcompositor.exe in CI only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pc/stream-bridge-source-audit-shared.h"
#include <d3d11.h>
#include <cstdio>
#include <string>
#include <vector>
using namespace stream_bridge::sender::source_audit;
namespace {
int target(DWORD server,bool single){
    wchar_t key[96]{};if(!name(server,key))return 1;
    const auto deadline=GetTickCount64()+15000;HANDLE mapping=nullptr;Page* page=nullptr;
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;int result=1;
    while(GetTickCount64()<deadline){
        if(!mapping){mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,key);
            if(mapping)page=static_cast<Page*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Page)));}
        if(page&&InterlockedCompareExchange(&page->stopped,0,0)){result=device?0:2;break;}
        stream_bridge::sender::SourceAuditStatus status{};
        if(page&&!device&&read(page->statusSequence,page->status,status)&&status.state==4){
            if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,single?D3D11_CREATE_DEVICE_SINGLETHREADED:0,
                nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)))break;
            context->Draw(0,0);
        }
        Sleep(10);
    }
    if(context){context->Draw(0,0);context->Release();}if(device)device->Release();
    if(page)UnmapViewOfFile(page);
    if(mapping)CloseHandle(mapping);
    return result;
}
}
int main(int argc,char** argv){
    if(argc==4&&std::string(argv[1])=="target")return target(static_cast<DWORD>(std::stoul(argv[2])),std::string(argv[3])=="single");
    if(argc!=3)return 1;
    const DWORD control=static_cast<DWORD>(std::stoul(argv[1]));
    wchar_t file[32768]{};if(!GetModuleFileNameW(nullptr,file,32768))return 1;
    const std::wstring self=file;const auto slash=self.find_last_of(L"\\/");if(slash==std::wstring::npos)return 1;
    const auto directory=self.substr(0,slash+1);
    const auto child=directory+(control==3?L"decoy\\vrcompositor.exe":L"vrcompositor.exe");
    const auto command=L"\""+child+L"\" target "+std::to_wstring(GetCurrentProcessId())+L" "+(std::string(argv[2])=="single"?L"single":L"normal");
    std::vector<wchar_t> mutableCommand(command.begin(),command.end());mutableCommand.push_back(0);
    STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};
    if(!CreateProcessW(child.c_str(),mutableCommand.data(),nullptr,nullptr,FALSE,0,nullptr,directory.c_str(),&startup,&process))return 1;
    const auto dll=LoadLibraryW((directory+L"audit_fixture.dll").c_str());bool pass=false;
    if(dll){using Run=BOOL(WINAPI*)(DWORD);Run run=nullptr;const auto symbol=GetProcAddress(dll,"AuditFixtureRun");
        static_assert(sizeof run==sizeof symbol,"Win64 function pointers");std::memcpy(&run,&symbol,sizeof run);
        pass=run&&run(control);}
    // The test child is owned by this fixture. Negative cases never started
    // its observer; terminate that waiting fixture rather than wait15seconds.
    if(control!=1)TerminateProcess(process.hProcess,0);
    const auto waited=WaitForSingleObject(process.hProcess,5000);DWORD code=1;
    if(waited==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);
    else TerminateProcess(process.hProcess,1);
    CloseHandle(process.hThread);CloseHandle(process.hProcess);
    if(dll)FreeLibrary(dll);
    std::printf("owned loader target exit=%lu; %s\n",static_cast<unsigned long>(code),pass&&code==0?"PASS":"FAIL");
    return pass&&code==0?0:1;
}
