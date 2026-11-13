#include "stream-bridge-source-audit-windows.h"
#include "stream-bridge-source-audit-shared.h"
#include "stream-bridge-source-audit-modules-windows.h"
#include <tlhelp32.h>
#include <sddl.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace stream_bridge::sender {
namespace {
using namespace source_audit;
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){reset();}
    void reset(HANDLE next=nullptr)noexcept{if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=next;}
    explicit operator bool()const noexcept{return value&&value!=INVALID_HANDLE_VALUE;}
};
std::wstring processPath(HANDLE process){
    std::vector<wchar_t> path(32768);DWORD count=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process,0,path.data(),&count)||!count)return {};
    return {path.data(),count};
}
std::wstring modulePath(HMODULE module){
    std::vector<wchar_t> path(32768);const auto count=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    return count&&count<path.size()?std::wstring(path.data(),count):std::wstring{};
}
bool sameUser(HANDLE left,HANDLE right){
    Handle a,b;
    if(!OpenProcessToken(left,TOKEN_QUERY,&a.value)||!OpenProcessToken(right,TOKEN_QUERY,&b.value))return false;
    DWORD na=0,nb=0;
    GetTokenInformation(a.value,TokenUser,nullptr,0,&na);GetTokenInformation(b.value,TokenUser,nullptr,0,&nb);
    if(!na||!nb||na>65536||nb>65536)return false;
    std::vector<unsigned char> va(na),vb(nb);
    return GetTokenInformation(a.value,TokenUser,va.data(),na,&na)&&
        GetTokenInformation(b.value,TokenUser,vb.data(),nb,&nb)&&
        EqualSid(reinterpret_cast<TOKEN_USER*>(va.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(vb.data())->User.Sid);
}
bool createPage(HANDLE& mapping,Page*& view){
    Handle token;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))return false;
    DWORD bytes=0;GetTokenInformation(token.value,TokenUser,nullptr,0,&bytes);if(!bytes||bytes>65536)return false;
    std::vector<unsigned char> data(bytes);
    if(!GetTokenInformation(token.value,TokenUser,data.data(),bytes,&bytes))return false;
    LPWSTR sid=nullptr;
    if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return false;
    const std::wstring sddl=L"D:P(A;;GRGW;;;"+std::wstring(sid)+L")";LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr))return false;
    SECURITY_ATTRIBUTES security{sizeof security,descriptor,FALSE};wchar_t path[96]{};
    if(!name(GetCurrentProcessId(),path)){LocalFree(descriptor);return false;}
    mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,&security,PAGE_READWRITE,0,sizeof(Page),path);
    const auto error=GetLastError();LocalFree(descriptor);
    if(!mapping||error==ERROR_ALREADY_EXISTS){if(mapping)CloseHandle(mapping);mapping=nullptr;return false;}
    view=static_cast<Page*>(MapViewOfFile(mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(Page)));
    if(!view){CloseHandle(mapping);mapping=nullptr;return false;}
    new(view) Page{};view->server=GetCurrentProcessId();return true;
}
}
struct SourceAuditServer::Impl {
    SourceAuditDiscovery discovery;
    explicit Impl(SourceAuditDiscovery value):discovery(value){}
    mutable std::mutex mutex;HANDLE mapping=nullptr;Page* page=nullptr;
    std::thread worker;
    Handle process,thread,sourceFile;DWORD target=0;std::uint64_t targetBirth=0;
    void* remoteArgument=nullptr;std::size_t remoteBytes=0;
    unsigned phase=0;bool stopped=false,attempted=false;
    std::uint64_t nextScan=0,deadline=0,nextInventory=0,inventoryEpoch=0,readinessDeadline=0;
    std::wstring ownPath;HMODULE ownModule=nullptr;
    SourceAuditStatus local{};
    bool eligible(DWORD pid,HANDLE handle)const {
        DWORD ownSession=0,targetSession=0;
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&ownSession)||!ProcessIdToSessionId(pid,&targetSession)||
            ownSession!=targetSession||!sameUser(GetCurrentProcess(),handle))return false;
        const auto self=processPath(GetCurrentProcess());const auto slash=self.find_last_of(L"\\/");
        if(slash==std::wstring::npos||_wcsicmp(self.c_str()+slash+1,L"vrserver.exe"))return false;
        return !_wcsicmp(processPath(handle).c_str(),(self.substr(0,slash+1)+L"vrcompositor.exe").c_str());
    }
    ~Impl(){stop();}
    void stop()noexcept{
        stopped=true;
        if(page)InterlockedExchange(&page->stopped,1);
        // An uncertain remote call owns its argument until completion/process
        // exit. Never free beneath the loader, terminate it, or replay it.
        if(remoteArgument&&process&&(!thread||WaitForSingleObject(thread.value,0)==WAIT_OBJECT_0))
            VirtualFreeEx(process.value,remoteArgument,0,MEM_RELEASE);
        // Keep the source file immutable too while an uncertain loader may
        // still open it. One bounded process-lifetime handle; Apply closes the
        // parent SteamVR process before any future package replacement.
        if(thread&&WaitForSingleObject(thread.value,0)!=WAIT_OBJECT_0)sourceFile.value=nullptr;
        remoteArgument=nullptr;thread.reset();process.reset();sourceFile.reset();
        if(page){UnmapViewOfFile(page);page=nullptr;}
        if(mapping){CloseHandle(mapping);mapping=nullptr;}
    }
    void failed(DWORD error)noexcept{local.failureStage=phase+1;local.state=8;local.error=error;phase=0;attempted=true;}
    bool begin(DWORD pid,VerifyFile verify){
        // Inspect only public identity with query rights first. A compositor
        // belonging to another user/session/installation is never opened for
        // writes and does not consume this instance's one bounded attempt.
        Handle candidate;candidate.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
        if(!candidate||!eligible(pid,candidate.value))return true;
        const auto expectedBirth=birth(candidate.value);if(!expectedBirth)return true;
        process.reset(OpenProcess(PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid));
        if(!process)return false;
        if(!eligible(pid,process.value)||birth(process.value)!=expectedBirth){SetLastError(ERROR_INVALID_HANDLE);return false;}
        const auto self=processPath(GetCurrentProcess());const auto slash=self.find_last_of(L"\\/");
        if(slash==std::wstring::npos||_wcsicmp(self.c_str()+slash+1,L"vrserver.exe"))return false;
        const auto expected=self.substr(0,slash+1)+L"vrcompositor.exe";
        if(_wcsicmp(processPath(process.value).c_str(),expected.c_str()))return false;
        Handle executable;executable.reset(CreateFileW(expected.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!executable)return false;
        targetBirth=birth(process.value);if(!targetBirth)return false;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&SFNP_CompositorSourceAuditStartV1),&ownModule))return false;
        ownPath=modulePath(ownModule);if(ownPath.empty())return false;
        // Never call an RVA in a pre-existing module whose loaded revision
        // cannot be established from public module metadata alone.
        sourceFile.reset(CreateFileW(ownPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!sourceFile)return false;
        const auto prior=findModule(pid,ownPath,discovery,sourceFile.value);
        if(prior.base||prior.matches){SetLastError(ERROR_ALREADY_EXISTS);return false;}
        if(prior.error!=ERROR_MOD_NOT_FOUND){
            if(prior.error==ERROR_BAD_LENGTH||prior.error==ERROR_PARTIAL_COPY){
                if(!readinessDeadline)readinessDeadline=GetTickCount64()+3000;
                if(GetTickCount64()<readinessDeadline){process.reset();sourceFile.reset();return true;}
            }
            SetLastError(prior.error);return false;
        }
        const auto load=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW");HMODULE owner=nullptr;
        if(!load||!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(load),&owner))return false;
        const auto ownerPath=modulePath(owner);
        const auto ownerImage=findModule(pid,ownerPath,discovery);
        const auto remoteBase=ownerImage.base,remoteSize=ownerImage.bytes;
        const auto offset=reinterpret_cast<std::uintptr_t>(load)-reinterpret_cast<std::uintptr_t>(owner);
        if(!remoteBase){
            // CreateProcess can expose the new PID before its public system
            // module list is initialized. Nothing has been written yet, so
            // bounded read-only discovery can wait without replaying a call.
            if(ownerImage.error==ERROR_BAD_LENGTH||ownerImage.error==ERROR_PARTIAL_COPY||ownerImage.error==ERROR_MOD_NOT_FOUND){
                if(!readinessDeadline)readinessDeadline=GetTickCount64()+3000;
                if(GetTickCount64()<readinessDeadline){process.reset();sourceFile.reset();return true;}
            }
            SetLastError(ownerImage.error);return false;
        }
        if(offset>=remoteSize){SetLastError(ERROR_INVALID_ADDRESS);return false;}
        if(!verify||!verify(expected.c_str(),executable.value)){SetLastError(ERROR_INVALID_EXE_SIGNATURE);return false;}
        remoteBytes=(ownPath.size()+1)*sizeof(wchar_t);
        if(remoteBytes>65536){SetLastError(ERROR_BAD_LENGTH);return false;}
        remoteArgument=VirtualAllocEx(process.value,nullptr,remoteBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        SIZE_T used=0;
        if(!remoteArgument||!WriteProcessMemory(process.value,remoteArgument,ownPath.c_str(),remoteBytes,&used)||used!=remoteBytes)return false;
        thread.reset(CreateRemoteThread(process.value,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteBase+offset),remoteArgument,0,nullptr));
        if(!thread)return false;
        ++local.loadCalls;target=pid;local.pid=pid;local.state=2;phase=1;attempted=true;deadline=GetTickCount64()+3000;return true;
    }
    void startObserver(){
        const auto module=findModule(target,ownPath,discovery,sourceFile.value);
        local.discoveryError=module.error;local.discoveryEntries=module.entries;local.discoveryMatches=module.matches;
        if(!module.base){
            ++local.discoveryMisses;
            if((module.error==ERROR_BAD_LENGTH||module.error==ERROR_PARTIAL_COPY||module.error==ERROR_MOD_NOT_FOUND)&&GetTickCount64()<deadline)return;
            failed(module.error);return;
        }
        const auto offset=reinterpret_cast<std::uintptr_t>(&SFNP_CompositorSourceAuditStartV1)-reinterpret_cast<std::uintptr_t>(ownModule);
        if(offset>=module.bytes){failed(ERROR_INVALID_ADDRESS);return;}
        const Launch launch{Magic,birth(GetCurrentProcess()),GetCurrentProcessId(),0};
        remoteArgument=VirtualAllocEx(process.value,nullptr,sizeof launch,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);SIZE_T used=0;
        if(!remoteArgument||!WriteProcessMemory(process.value,remoteArgument,&launch,sizeof launch,&used)||used!=sizeof launch){failed(GetLastError());return;}
        thread.reset(CreateRemoteThread(process.value,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(module.base+offset),remoteArgument,0,nullptr));
        if(!thread){failed(GetLastError());return;}
        ++local.entryCalls;phase=2;local.state=3;deadline=GetTickCount64()+3000;
    }
    void advance(){
        if(!process||WaitForSingleObject(process.value,0)==WAIT_OBJECT_0){failed(ERROR_PROCESS_ABORTED);return;}
        if(birth(process.value)!=targetBirth){failed(ERROR_INVALID_HANDLE);return;}
        if(phase==4){startObserver();return;}
        if(WaitForSingleObject(thread.value,0)!=WAIT_OBJECT_0){if(GetTickCount64()>=deadline)failed(WAIT_TIMEOUT);return;}
        DWORD result=0;if(!GetExitCodeThread(thread.value,&result)){failed(GetLastError());return;}
        local.remoteResult=result;
        thread.reset();
        if(remoteArgument){VirtualFreeEx(process.value,remoteArgument,0,MEM_RELEASE);remoteArgument=nullptr;}
        // A remote LoadLibrary thread exposes only DWORD exit status; never
        // reinterpret it as a 64-bit HMODULE. Module metadata below is checked.
        if(phase==2){if(!result){failed(ERROR_DLL_INIT_FAILED);return;}phase=3;local.state=3;return;}
        // Loading is complete, but Windows' public module snapshot can still
        // report loader churn. Repeat only metadata discovery, never loading.
        phase=4;deadline=GetTickCount64()+3000;startObserver();
    }
};
SourceAuditServer::SourceAuditServer(SourceAuditDiscovery discovery):impl_(new Impl(discovery)){}
SourceAuditServer::~SourceAuditServer(){stop();}
bool SourceAuditServer::start(VerifyFile verify,Inventory inventory,void* opaque)noexcept{
    auto& s=*impl_;std::lock_guard<std::mutex> guard(s.mutex);
    if(!verify||s.stopped||s.worker.joinable())return false;
    try{s.worker=std::thread([this,verify,inventory,opaque]{
        for(;;){
            {std::lock_guard<std::mutex> lock(impl_->mutex);if(impl_->stopped)break;}
            tick(verify,inventory,opaque);Sleep(2);
        }
    });return true;}catch(...){s.failed(ERROR_NOT_ENOUGH_MEMORY);return false;}
}
void SourceAuditServer::tick(VerifyFile verify,Inventory getInventory,void* opaque)noexcept{
    auto& s=*impl_;std::lock_guard<std::mutex> guard(s.mutex);if(s.stopped)return;
    try{
        if(!s.page&&!s.mapping){if(!createPage(s.mapping,s.page)){s.failed(GetLastError());s.stopped=true;return;}s.local.state=1;}
        const auto now=GetTickCount64();
        if(now>=s.nextInventory){s.nextInventory=now+250;SourceAuditInventory inventory{};
            if(getInventory&&getInventory(s.inventoryEpoch,inventory,opaque)&&inventory.epoch&&inventory.epoch!=s.inventoryEpoch){
                publish(s.page->inventorySequence,s.page->inventory,inventory);s.inventoryEpoch=inventory.epoch;
            }
        }
        if(s.phase==1||s.phase==2||s.phase==4){s.advance();return;}
        if(s.attempted)return;
        if(now<s.nextScan)return;
        s.nextScan=now+10;
        Handle snapshot;snapshot.reset(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(!snapshot)return;
        PROCESSENTRY32W entry{};entry.dwSize=sizeof entry;
        if(Process32FirstW(snapshot.value,&entry))do{if(!_wcsicmp(entry.szExeFile,L"vrcompositor.exe")){
            if(!s.begin(entry.th32ProcessID,verify)){s.failed(GetLastError());}
            if(s.attempted)break;
        }}while(Process32NextW(snapshot.value,&entry));
    }catch(...){s.failed(ERROR_UNHANDLED_EXCEPTION);}
}
SourceAuditStatus SourceAuditServer::status()const noexcept{
    auto& s=*impl_;std::lock_guard<std::mutex> guard(s.mutex);SourceAuditStatus result=s.local,remote{};
    if(s.local.state!=8&&s.page&&s.target&&read(s.page->statusSequence,s.page->status,remote)&&remote.pid==s.target){
        remote.discoveryError=s.local.discoveryError;remote.discoveryEntries=s.local.discoveryEntries;
        remote.discoveryMatches=s.local.discoveryMatches;remote.discoveryMisses=s.local.discoveryMisses;
        remote.loadCalls=s.local.loadCalls;remote.entryCalls=s.local.entryCalls;
        remote.sampleCount=std::min(remote.sampleCount,SourceAuditSamples);result=remote;
    }
    return result;
}
void SourceAuditServer::stop()noexcept{
    // Do not join while holding the lock needed by the discovery worker.
    std::thread worker;
    {std::lock_guard<std::mutex> guard(impl_->mutex);impl_->stop();worker=std::move(impl_->worker);}
    if(worker.joinable())worker.join();
}
} // namespace stream_bridge::sender
