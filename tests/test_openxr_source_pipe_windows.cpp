#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "openxr/openxr_source_windows.h"
#include "pc/stream-bridge-sender-provider-sourceipc.h"
#include "pc/stream-bridge-source-contract.h"
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
using namespace stream_bridge;using namespace stream_bridge::sender;using namespace stream_bridge::openxr;
namespace {
unsigned checks=0;void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
void await(const std::function<bool()>& ready,const char* why,ULONGLONG timeout=5000){
    const auto end=GetTickCount64()+timeout;while(!ready()){check(GetTickCount64()<end,why);Sleep(2);}++checks;
}
bool trustSelf(std::uint32_t pid){return pid==GetCurrentProcessId();}
bool trustNobody(std::uint32_t){return false;}
// The provider's own source server, ticked like its cold worker.
struct Provider {
    SourceContract contract;std::atomic<bool> ready{false},stop{false};std::atomic<unsigned> preparations{0};
    SourceIpcServer server{[](std::uint32_t,void* p){auto& s=*static_cast<Provider*>(p);++s.preparations;return s.ready.load();},this,
        [](std::uint32_t pid,bool budget,void* p)noexcept{static_cast<Provider*>(p)->contract.publish(pid,budget);}};
    std::thread worker;
    Provider(){worker=std::thread([this]{SourceIpcState peer;peer.compatible=true;peer.generation=19;peer.binding[0]=31;while(!stop){server.tick(peer);Sleep(1);}server.stop();});}
    ~Provider(){stop=true;worker.join();}
};
// An older sender: closes on a v2 request and grants a v1 request.
void legacyServer(std::atomic<bool>& stop,std::atomic<unsigned>& v2Refused){
    wchar_t name[96]{};std::swprintf(name,96,L"\\\\.\\pipe\\SFNP.StreamBridge.V1.%lu",GetCurrentProcessId());
    while(!stop){
        HANDLE pipe=CreateNamedPipeW(name,PIPE_ACCESS_DUPLEX,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,128,128,0,nullptr);
        if(pipe==INVALID_HANDLE_VALUE)return;
        if(ConnectNamedPipe(pipe,nullptr)||GetLastError()==ERROR_PIPE_CONNECTED){
            SourceIpcMessage request{};DWORD bytes=0;ULONG pid=0;SourceIpcRequest client;
            if(ReadFile(pipe,request.data(),128,&bytes,nullptr)&&bytes==128&&GetNamedPipeClientProcessId(pipe,&pid)&&readSourceRequest(request,pid,client)){
                if(client.metadataBudget)++v2Refused;
                else{Nonce binding{};binding[0]=3;const auto reply=sourceReply(client,GetCurrentProcessId(),binding,23,true);WriteFile(pipe,reply.data(),128,&bytes,nullptr);
                    unsigned char extra{};ReadFile(pipe,&extra,1,&bytes,nullptr);}
            }
        }
        DisconnectNamedPipe(pipe);CloseHandle(pipe);
    }
}
void run(){
    const auto pid=GetCurrentProcessId();
    check(!probeSource(200,trustSelf),"no pipe, no capability");
    {
        Provider provider;
        await([&]{return probeSource(300,trustSelf);},"present sender answers the probe before admitting anything");
        check(!provider.contract.snapshot(pid),"a probe never leaves a declaration");
        check(!probeSource(300,trustNobody),"an untrusted pipe server is refused");
        check(!steamVrServer(pid),"the test process is not the SteamVR server");
        SourceClient client(trustSelf);check(!client.state().connected,"a new client is quiescent");
        client.want(true);
        await([&]{auto s=client.state();return s.connected&&!s.granted;},"connected and waiting for admission");
        provider.ready=true;
        await([&]{return client.state().granted;},"admission arrives on the live connection");
        auto state=client.state();check(state.budget&&state.generation==19,"v2 grant with the provider generation");
        check(provider.contract.snapshot(pid)&&provider.contract.budgetAuthority(pid),"the provider publishes this process as the source");
        const auto first=provider.contract.snapshot(pid);Sleep(300);check(client.state().granted,"a healthy idle connection keeps its grant");
        client.want(false);
        await([&]{return !client.state().connected&&!provider.contract.snapshot(pid);},"withdrawal closes the declaration");
        client.want(true);
        await([&]{return client.state().granted&&provider.contract.snapshot(pid);},"a new declaration after withdrawal",8000);
        check(provider.contract.snapshot(pid)!=first,"a new declaration is a new generation");
    }
    {
        Provider provider;provider.ready=true;
        {SourceClient client(trustSelf);client.want(true);await([&]{return client.state().granted;},"grant before destruction");}
        await([&]{return !provider.contract.snapshot(pid);},"destroying the client revokes");
        SourceClient client(trustSelf);client.want(true);await([&]{return client.state().granted;},"grant before the sender exits");
        provider.stop=true;provider.worker.join();provider.worker=std::thread([]{});
        await([&]{return !client.state().connected&&!client.state().granted;},"sender exit revokes at once");
    }
    {
        std::atomic<bool> stop{false};std::atomic<unsigned> refused{0};std::thread legacy([&]{legacyServer(stop,refused);});
        {
            SourceClient client(trustSelf);client.want(true);
            await([&]{return client.state().granted;},"an older sender is asked again with v1");
            const auto state=client.state();check(!state.budget&&state.generation==23&&refused.load()>=1,"v1 grant after a refused v2 request");
        }
        stop=true;HANDLE wake=CreateFileW((L"\\\\.\\pipe\\SFNP.StreamBridge.V1."+std::to_wstring(GetCurrentProcessId())).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        if(wake!=INVALID_HANDLE_VALUE)CloseHandle(wake);legacy.join();
    }
}
}
int main(){try{run();std::printf("OpenXR layer source pipe: %u checks PASS\n",checks);return 0;}catch(const std::exception&e){std::fprintf(stderr,"OpenXR layer source pipe failed after %u: %s\n",checks,e.what());return 1;}}
