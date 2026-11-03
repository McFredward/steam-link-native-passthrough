#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "openxr/openxr_source_windows.h"
#include "openxr/openxr_source_record.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace stream_bridge::openxr {
namespace {
std::wstring lower(std::wstring text){for(auto& c:text){if(c==L'/')c=L'\\';c=wchar_t(std::towlower(c));}while(!text.empty()&&text.back()==L'\\')text.pop_back();return text;}
// The "runtime" string array of openvrpaths.vrpath, without a JSON dependency.
std::vector<std::wstring> runtimeDirectories(){
    std::vector<std::wstring> out;wchar_t base[MAX_PATH]{};
    const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",base,MAX_PATH);if(!length||length>=MAX_PATH)return out;
    std::ifstream file(std::filesystem::path(std::wstring(base)+L"\\openvr\\openvrpaths.vrpath"),std::ios::binary);if(!file)return out;
    std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());if(text.size()>1<<20)return out;
    auto at=text.find("\"runtime\"");if(at==std::string::npos)return out;
    at=text.find('[',at);const auto end=text.find(']',at);if(at==std::string::npos||end==std::string::npos)return out;
    for(auto i=at;i<end;++i){
        if(text[i]!='"')continue;std::string value;
        for(++i;i<end&&text[i]!='"';++i){if(text[i]=='\\'&&i+1<end)++i;value+=text[i];}
        const int wide=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
        if(wide<=0)continue;std::wstring path(std::size_t(wide),L'\0');
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),path.data(),wide);out.push_back(lower(path));
    }
    return out;
}
bool randomNonce(SourceNonce& nonce)noexcept{
    return BCryptGenRandom(nullptr,nonce.data(),ULONG(nonce.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0&&nonzero(nonce);
}
// The sender's pipe carries its server PID in the name; the kernel's server
// PID must match it and pass the trust rule. Listing the namespace opens nothing.
HANDLE openSource(TrustedServer trusted,std::uint32_t& server,DWORD waitMs){
    constexpr wchar_t prefix[]=L"SFNP.StreamBridge.V1.";constexpr std::size_t prefixLength=sizeof(prefix)/sizeof(prefix[0])-1;
    WIN32_FIND_DATAW entry{};const HANDLE find=FindFirstFileW(L"\\\\.\\pipe\\*",&entry);if(find==INVALID_HANDLE_VALUE)return INVALID_HANDLE_VALUE;
    HANDLE result=INVALID_HANDLE_VALUE;unsigned candidates=0;
    do{
        const std::wstring name(entry.cFileName);
        if(name.compare(0,prefixLength,prefix)||name.size()==prefixLength||name.size()>prefixLength+10)continue;
        std::uint64_t pid=0;bool digits=true;for(auto i=prefixLength;i<name.size();++i){const auto c=name[i];if(c<L'0'||c>L'9'){digits=false;break;}pid=pid*10+unsigned(c-L'0');}
        if(!digits||!pid||pid>0xffffffffu||++candidates>8)continue;
        const auto path=L"\\\\.\\pipe\\"+name;
        for(unsigned attempt=0;attempt<2&&result==INVALID_HANDLE_VALUE;++attempt){
            const HANDLE pipe=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
            if(pipe==INVALID_HANDLE_VALUE){if(GetLastError()!=ERROR_PIPE_BUSY||!WaitNamedPipeW(path.c_str(),waitMs))break;continue;}
            ULONG actual=0;
            if(GetNamedPipeServerProcessId(pipe,&actual)&&actual==pid&&trusted&&trusted(actual)){result=pipe;server=actual;}
            else{CloseHandle(pipe);break;}
        }
    }while(result==INVALID_HANDLE_VALUE&&FindNextFileW(find,&entry));
    FindClose(find);return result;
}
// Bounded overlapped transfer; wake is signalled when the caller should give up.
bool transfer(HANDLE pipe,bool writing,void* data,DWORD length,HANDLE wake,DWORD waitMs){
    OVERLAPPED op{};op.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!op.hEvent)return false;
    DWORD bytes=0;bool done=writing?WriteFile(pipe,data,length,&bytes,&op)!=0:ReadFile(pipe,data,length,&bytes,&op)!=0;
    if(!done&&GetLastError()==ERROR_IO_PENDING){
        const HANDLE events[2]{op.hEvent,wake};const DWORD count=wake?2:1;
        const auto wait=WaitForMultipleObjects(count,events,FALSE,waitMs);
        if(wait!=WAIT_OBJECT_0){CancelIoEx(pipe,&op);GetOverlappedResult(pipe,&op,&bytes,TRUE);CloseHandle(op.hEvent);return false;}
        done=GetOverlappedResult(pipe,&op,&bytes,FALSE)!=0;
    }
    CloseHandle(op.hEvent);return done&&bytes==length; // Byte-mode pipes deliver 128-byte records whole here.
}
} // namespace

bool steamVrServer(std::uint32_t pid)noexcept{
    try{
        const HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return false;
        std::wstring image(32768,L'\0');DWORD size=DWORD(image.size());
        const bool named=QueryFullProcessImageNameW(process,0,image.data(),&size)!=0;CloseHandle(process);if(!named)return false;
        image=lower(image.substr(0,size));
        for(const auto& root:runtimeDirectories())if(!root.empty()&&image.size()>root.size()&&!image.compare(0,root.size(),root)&&image[root.size()]==L'\\')return true;
    }catch(...){}
    return false;
}
bool probeSource(unsigned waitMs,TrustedServer trusted)noexcept{
    try{
        const auto start=GetTickCount64();std::uint32_t server=0;
        const auto left=[&]{const auto spent=GetTickCount64()-start;return DWORD(spent>=waitMs?0:waitMs-spent);};
        // The sender re-creates its single pipe instance one worker tick after a
        // client leaves; an absent pipe is looked for again for up to 100 ms.
        HANDLE pipe=INVALID_HANDLE_VALUE;
        for(;;){pipe=openSource(trusted,server,left());if(pipe!=INVALID_HANDLE_VALUE||GetTickCount64()-start>=std::min<ULONGLONG>(waitMs,100))break;Sleep(10);}
        if(pipe==INVALID_HANDLE_VALUE)return false;
        SourceNonce nonce{};bool answered=false;
        // A v1-only sender closes on a v2 request; the probe asks v1, which every sender answers.
        if(randomNonce(nonce)){
            auto request=sourceRequest(GetCurrentProcessId(),nonce,false);SourceMessage reply{};
            answered=transfer(pipe,true,request.data(),DWORD(request.size()),nullptr,left())&&transfer(pipe,false,reply.data(),DWORD(reply.size()),nullptr,left())&&
                readSourceReply(reply,GetCurrentProcessId(),server,nonce,false).valid;
        }
        CloseHandle(pipe);return answered;
    }catch(...){return false;}
}

struct SourceClient::Impl {
    TrustedServer trusted;std::mutex mutex;std::condition_variable changed;bool wanted=false,stopping=false;
    HANDLE wake=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic<std::uint64_t> generation{0};std::atomic<unsigned> flags{0};
    bool legacy=false;std::thread worker;
    explicit Impl(TrustedServer rule):trusted(rule){if(wake)worker=std::thread([this]{run();});}
    ~Impl(){{std::lock_guard<std::mutex> guard(mutex);stopping=true;}SetEvent(wake);changed.notify_all();if(worker.joinable())worker.join();if(wake)CloseHandle(wake);}
    bool active(){std::lock_guard<std::mutex> guard(mutex);return wanted&&!stopping;}
    void publish(bool connected,const SourceReply& reply){
        flags.store((connected?1u:0u)|(reply.granted?2u:0u)|(reply.budget?4u:0u),std::memory_order_release);
        generation.store(reply.granted?reply.generation:0,std::memory_order_release);
    }
    // One conversation. Returns whether any valid reply arrived.
    bool converse(bool budget){
        std::uint32_t server=0;const HANDLE pipe=openSource(trusted,server,250);if(pipe==INVALID_HANDLE_VALUE)return false;
        SourceNonce nonce{};bool answered=false;
        if(randomNonce(nonce)){
            const auto pid=GetCurrentProcessId();auto request=sourceRequest(pid,nonce,budget);
            if(transfer(pipe,true,request.data(),DWORD(request.size()),wake,2000)){
                // The first reply is due within 2 s; a healthy idle connection has no expiry.
                while(active()){
                    SourceMessage message{};if(!transfer(pipe,false,message.data(),DWORD(message.size()),wake,answered?INFINITE:2000))break;
                    const auto reply=readSourceReply(message,pid,server,nonce,budget);if(!reply.valid)break;
                    answered=true;publish(true,reply);
                }
            }
        }
        CloseHandle(pipe);publish(false,{});return answered;
    }
    void run()noexcept{
        for(;;){
            {std::unique_lock<std::mutex> lock(mutex);changed.wait(lock,[&]{return stopping||wanted;});if(stopping)return;ResetEvent(wake);}
            try{
                // An older sender rejects or closes a v2 request: ask again with v1 and a fresh nonce.
                if(!converse(!legacy)&&!legacy&&active())legacy=converse(false);
            }catch(...){publish(false,{});}
            std::unique_lock<std::mutex> lock(mutex);changed.wait_for(lock,std::chrono::seconds(1),[&]{return stopping||!wanted;});
        }
    }
};
SourceClient::SourceClient(TrustedServer trusted):impl_(new Impl(trusted)){}
SourceClient::~SourceClient()=default;
void SourceClient::want(bool value)noexcept{
    {std::lock_guard<std::mutex> guard(impl_->mutex);if(impl_->wanted==value)return;impl_->wanted=value;}
    if(!value)SetEvent(impl_->wake); // Withdrawal cancels the live read at once.
    impl_->changed.notify_all();
}
SourceState SourceClient::state()const noexcept{
    const auto f=impl_->flags.load(std::memory_order_acquire);
    return {bool(f&1),bool(f&2),bool(f&4),impl_->generation.load(std::memory_order_acquire)};
}
} // namespace stream_bridge::openxr
