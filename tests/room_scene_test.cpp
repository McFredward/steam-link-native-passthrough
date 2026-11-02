#include "room_scan/native_scene_client.h"
#include <cstdio>
#include <filesystem>
using namespace room_native;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void refuses(F call,const char* reason){bool refused=false;try{call();}catch(const std::exception&){refused=true;}check(refused,reason);}
void waitEvent(RoomSceneClient& client,const std::string& name){
    for(unsigned n=0;n<400;++n){const auto packet=client.receive();if(packet&&packet->value("event","")==name)return;
        if(client.pollExit())break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    throw std::runtime_error("Owned scene fixture missed "+name);
}
}
int main(int argc,char** argv){try{
    check(argc==2,"explicit fixture binary required");
    SceneOriginRegistration registration;const auto root=pose({1.2f,-.3f,2.1f},{0,std::sin(.2f),0,std::cos(.2f)});
    for(unsigned i=0;i<3;++i){const float angle=float(i)*.15f;
        const auto head=pose({float(i)*.1f,1.6f,-.2f},{0,std::sin(angle),0,std::cos(angle)});
        const bool ready=registration.observe(multiply(root,head),head,1000000000u+i*50000000u,100000);
        check(ready==(i==2),"registration needs independent timed observations before showing geometry");}
    const V3 landmark{.4f,1.3f,-2.5f};
    check(length(transform(registration.roomFromStage(),landmark)-transform(root,landmark))<1e-5f,"measured bridge maps an independent room landmark correctly");
    const auto frozen=registration.roomFromStage();const auto head=pose({.4f,1.6f,.2f},{0,0,0,1});
    registration.observe(multiply(root,head),head,1200000000,100000);
    check(length(position(registration.roomFromStage())-position(frozen))<1e-7f,"verified origin is retained rather than following head noise");
    auto changed=root;changed.m[0][3]+=.02f;
    refuses([&]{registration.observe(multiply(changed,head),head,1300000000,100000);},"a changed tracking origin cannot move a verified room silently");
    SceneOriginRegistration uncertain;
    refuses([&]{uncertain.observe(Mat{},Mat{},1,800000);},"uncertain clock pairing does not establish an origin");
    refuses([&]{sceneMatrix(nlohmann::json{{1,0,0,0},{0,2,0,0},{0,0,1,0}});},"non-rigid transforms cannot cross the scene pipe");
    int pair[2];check(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0,pair),"private control socket");
    check(sendScenePacket(pair[0],{{"event","hide"}}),"send bounded control");
    check(receiveScenePacket(pair[1])->at("event")=="hide","control arrives as a complete packet");
    const std::string large(ScenePacketLimit+1,'x');check(send(pair[0],large.data(),large.size(),MSG_NOSIGNAL)==ssize_t(large.size()),"oversized fixture packet sent");
    refuses([&]{receiveScenePacket(pair[1]);},"truncated packets cannot become scene control");close(pair[0]);close(pair[1]);
    char temporary[]="/tmp/sfnp-room-scene-fixture-XXXXXX";check(mkdtemp(temporary)!=nullptr,"owned fixture directory");
    struct Cleanup{const char* path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{temporary};
    const auto executable=std::filesystem::path(temporary)/"fixture";std::filesystem::copy_file(argv[1],executable);
    chmod(executable.c_str(),0770);check(!trustedSceneExecutable(executable.string()),"group-writable renderer is refused");
    chmod(executable.c_str(),0700);
    RoomSceneClient client;check(client.start(executable.string(),"unused-fixture-only"),"launch owned fixture");waitEvent(client,"fixture_ready");
    auto state=std::make_shared<const nlohmann::json>(nlohmann::json{{"world_revision",3},{"payload",std::string(65536,'a')}});
    client.snapshot(state);waitEvent(client,"snapshot_ack");
    check(client.control({{"event","hide"}}),"independent control remains available");waitEvent(client,"control_ack");
    client.snapshot(std::make_shared<const nlohmann::json>(nlohmann::json{{"world_revision",4},{"payload",std::string(15*1024*1024,'b')}}));
    const auto start=std::chrono::steady_clock::now();client.requestStop();
    while(client.running()&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<2){client.pollExit();std::this_thread::sleep_for(std::chrono::milliseconds(5));}
    check(!client.running(),"Stop does not wait behind a large geometry snapshot");
    std::puts("PASS measured origin registration, bounded packets and owned renderer Stop during queued geometry");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
