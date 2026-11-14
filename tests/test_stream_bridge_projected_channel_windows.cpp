#include "pc/stream-bridge-projected-alpha-channel.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

using namespace stream_bridge::sender::projected_alpha;
namespace {
unsigned checks=0;
void check(bool pass,const char* why){++checks;if(!pass)throw std::runtime_error(why);}
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Child {
    Handle process,thread;
    ~Child(){if(process.value&&WaitForSingleObject(process.value,0)==WAIT_TIMEOUT){TerminateProcess(process.value,91);WaitForSingleObject(process.value,2000);}}
};
int peer(HANDLE input){
    Handle owned;owned.value=input;ChannelLaunch launch{};DWORD read=0;
    check(ReadFile(input,&launch,sizeof launch,&read,nullptr)&&read==sizeof launch,"child receives one exact private launch through owned anonymous pipe");
    {auto bad=launch;bad.peerBirth^=1;ChannelPeer refused;check(!refused.open(bad),"wrong peer birth is refused without consuming the real handle");}
    {auto bad=launch;bad.cookie[0]^=1;ChannelPeer refused;check(!refused.open(bad),"wrong private cookie is refused without closing caller handle");}
    {auto bad=launch;bad.parentBirth^=1;ChannelPeer refused;check(!refused.open(bad),"different parent instance is refused");}
    ChannelPeer channel;check(channel.open(launch)&&channel.active(),"exact kernel-duplicated section and live parent accepted");
    Registry registry{};check(channel.page()->registry.read(registry)&&registry.sourceLease==0x70000004du&&registry.retained[0].handle==101,
        "private parent registry reaches exact child without public name lookup");
    ImageWitness witness{};witness.sourceLease=registry.sourceLease;witness.native=registry.retained[0];
    witness.scene=registry.retained[1];witness.maskHandle=909;witness.maskGeneration=12;
    // A transport test has no image proof and MUST NOT mark this as complete.
    check(channel.page()->witnesses[0].publish(witness),"private metadata payload published with no invented frame permit");
    const auto deadline=GetTickCount64()+3000;
    while(channel.active()&&GetTickCount64()<deadline)Sleep(1); // Own IPC fixture only, never a render callback.
    check(!channel.active(),"explicit parent stop invalidates the child channel");return 0;
}
void parent(){
    ChannelParent channel;check(channel.create(),"cold unnamed private section");
    Registry registry{};registry.revision=5;registry.sourceLease=0x70000004du;
    registry.retained[0]={101,18,100,37,29,28,1,1};registry.retained[1]={102,19,77,37,29,28,1,1};
    channel.sourceLease(registry.sourceLease);
    check(channel.page()->registry.publish(registry),"transport-only dummy metadata published without claiming real kernel pins");
    ImageWitness absent{};check(!channel.page()->witnesses[0].read(absent),"empty row supplies no image proof");
    SECURITY_ATTRIBUTES security{sizeof security,nullptr,TRUE};Handle input,output;
    check(CreatePipe(&input.value,&output.value,&security,4096)&&SetHandleInformation(output.value,HANDLE_FLAG_INHERIT,0),"owned anonymous launch pipe");
    std::vector<wchar_t> path(32768);const auto length=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    check(length&&length<path.size(),"owned fixture executable path");
    std::wstring command=L"\""+std::wstring(path.data(),length)+L"\" --projection-channel-child "+std::to_wstring(reinterpret_cast<std::uintptr_t>(input.value));
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> attributes(bytes);
    auto* list=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
    check(InitializeProcThreadAttributeList(list,1,0,&bytes),"bounded child handle-list attributes");
    struct DeleteAttributes {PPROC_THREAD_ATTRIBUTE_LIST value;~DeleteAttributes(){DeleteProcThreadAttributeList(value);}} cleanup{list};
    check(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&input.value,sizeof input.value,nullptr,nullptr),"only the owned pipe handle is inherited");
    STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof startup;startup.lpAttributeList=list;PROCESS_INFORMATION process{};
    check(CreateProcessW(path.data(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process),"owned child created suspended before private handle transfer");
    Child child;child.process.value=process.hProcess;child.thread.value=process.hThread;
    ChannelLaunch launch{};
    check(channel.duplicateToVerifiedPeer(child.process.value,launch)&&launch.peerPid!=GetCurrentProcessId(),"kernel section handle duplicated into exact separate process");
    ChannelLaunch duplicate{};check(!channel.duplicateToVerifiedPeer(child.process.value,duplicate),"uncertain or completed launch is never duplicated twice");
    DWORD written=0;
    check(WriteFile(output.value,&launch,sizeof launch,&written,nullptr)&&written==sizeof launch,"bounded private launch transfer");
    check(ResumeThread(child.thread.value)!=DWORD(-1),"owned child starts after handle transfer");
    ImageWitness witness{};bool received=false;const auto deadline=GetTickCount64()+3000;
    while(GetTickCount64()<deadline){
        if(channel.page()->witnesses[0].read(witness)){received=true;break;}
        if(WaitForSingleObject(child.process.value,0)==WAIT_OBJECT_0)break;
        Sleep(1); // Fixture only; production never waits for a witness.
    }
    check(received&&witness.sourceLease==registry.sourceLease&&witness.native.handle==101&&witness.scene.handle==102&&
        witness.maskHandle==909&&!witness.complete,"exact child metadata received; private channel alone does not authorize an image");
    channel.stop();check(WaitForSingleObject(child.process.value,3000)==WAIT_OBJECT_0,"owned peer shuts down after explicit channel stop");
    DWORD result=99;check(GetExitCodeProcess(child.process.value,&result)&&result==0,"child birth/cookie/parent and stop controls all pass");
}
}
int main(int argc,char** argv){try{
    if(argc==3&&!std::strcmp(argv[1],"--projection-channel-child")){
        char* end=nullptr;const auto value=std::strtoull(argv[2],&end,10);
        check(value&&end&&!*end,"bounded inherited launch handle argument");
        return peer(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value)));
    }
    check(argc==1,"no unrecognized fixture mode");parent();
    std::printf("PASS %u private cross-process channel checks; no native image/source permit\n",checks);return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,error.what());return 1;}}
