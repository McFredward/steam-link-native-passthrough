#include "probe/world_stability_core.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace world_stability;
namespace {
unsigned checks=0;
void check(bool condition,const char* reason){++checks;if(!condition)throw std::runtime_error(reason);}
Matrix identity(){Matrix result{};result.m[0][0]=result.m[1][1]=result.m[2][2]=1;return result;}
struct Text:Sink {
    std::string value;bool fail=false;
    bool write(const std::string& line)override{if(fail)return false;value+=line;return true;}
};
struct Fake:Api {
    std::vector<std::string> calls;unsigned stage=0,created=0;bool cleanup=true;
    Matrix initial=identity(),frozen{};Sample current{};
    Fake(){current.head=current.framePose=identity();current.headValid=current.frameValid=current.framePoseValid=true;current.beginMicros=1;current.endMicros=2;}
    bool initialize()override{calls.push_back("initialize");return stage!=1;}
    bool head(Matrix& m)override{calls.push_back("head");m=initial;return stage!=2;}
    bool create(const Matrix& m,Handle& h)override{calls.push_back("create");frozen=m;++created;h=7;return stage!=3;}
    bool sample(Sample& s)override{calls.push_back("sample");s=current;return stage!=4;}
    bool hide(Handle)override{calls.push_back("hide");return cleanup;}
    bool clear(Handle)override{calls.push_back("clear");return cleanup;}
    bool destroy(Handle)override{calls.push_back("destroy");return cleanup;}
    void shutdown()override{calls.push_back("shutdown");}
};
void geometry(){
    auto head=identity();head.m[0][3]=2;head.m[1][3]=1.6f;head.m[2][3]=3;
    auto reference=anchor(head);
    check(rigid(head)&&rigid(reference),"rigid head and reference");
    check(std::abs(reference.m[2][3]-1.8f)<1e-5f&&reference.m[0][3]==2,"reference is forward in world space");
    head.m[0][0]=0;head.m[0][2]=1;head.m[2][0]=-1;head.m[2][2]=0;
    reference=anchor(head);
    check(rigid(reference)&&std::abs(reference.m[0][3]-.8f)<1e-5f&&reference.m[2][3]==3,"head rotation determines initial placement");
    head.m[0][0]=std::numeric_limits<float>::quiet_NaN();check(!rigid(head),"NaN anchor refused");
    head=identity();head.m[0][0]=-1;check(!rigid(head),"reflection is not a head rotation");
    head=identity();head.m[0][0]=2;check(!rigid(head),"scaled tracking matrix refused");
    const auto pixels=pattern();
    check(pixels[128*TextureSize+128].a==255&&pixels[0].a==0,"opaque center and transparent exterior");
    check(pixels[128*TextureSize+128].r==255&&pixels[131*TextureSize+100].r==0,"white cross has opaque black outline");
}
void frozenReference(){
    Fake api;Text text;Output output(text);Session session(api,output);
    check(!session.start(false)&&api.calls.empty(),"no API activity without explicit opt-in");
    check(session.start(true),"reference starts");
    const auto frozen=api.frozen;
    for(unsigned n=0;n<200;++n){api.current.head.m[0][3]=float(n)/20;check(session.tick(),"movement sample recorded");}
    check(api.created==1&&api.frozen.m[0][3]==frozen.m[0][3]&&api.frozen.m[2][3]==frozen.m[2][3],"later head motion never reanchors the overlay");
    check(session.command("yaw")&&session.tick()&&text.value.find("\"phase\":1")!=std::string::npos,"phase labels are explicit");
    check(session.stop()&&session.stop(),"cleanup is idempotent");
    const std::vector<std::string> tail(api.calls.end()-4,api.calls.end());
    check(tail==std::vector<std::string>{"hide","clear","destroy","shutdown"},"owned overlay removed before SDK shutdown");
    check(text.value.find("\"exact_stream_pose_proven\":false")!=std::string::npos,"public context cannot claim a proved video pose");
}
void failures(){
    for(unsigned stage=1;stage<=3;++stage){
        Fake api;api.stage=stage;Text text;Output output(text);Session session(api,output);
        check(!session.start(true)&&!session.active(),"partial startup fails closed");
        check(api.calls.back()=="shutdown","partial SDK startup is shut down");
        if(stage==3)check(api.calls[api.calls.size()-2]=="destroy","partial created overlay removed");
    }
    for(bool origin:{true,false}){
        Fake api;Text text;Output output(text);Session session(api,output);check(session.start(true),"event fixture starts");
        api.current.originChanged=origin;api.current.runtimeQuit=!origin;
        check(!session.tick()&&!session.active(),"origin reset/quit ends instead of moving reference");
        check(api.created==1&&api.calls.back()=="shutdown","event cleanup retains single ownership");
    }
    Fake api;Text text;Output output(text);Session session(api,output);check(session.start(true),"failure fixture starts");
    api.current.angular[0]=std::numeric_limits<float>::quiet_NaN();check(session.tick(),"bad numeric observation retained as unavailable");
    check(text.value.find("\"numeric_valid\":false")!=std::string::npos&&text.value.find("nan")==std::string::npos,"JSON never emits nonfinite numbers");
    api.cleanup=false;check(!session.stop()&&text.value.find("\"clean\":false")!=std::string::npos,"failed cleanup never claims success");
    for(bool command:{false,true}){
        Fake broken;Text log;Output out(log);Session failure(broken,out);check(failure.start(true),"error fixture starts");
        if(command)check(!failure.command("unknown"),"unknown phase input stops the experiment");
        else{broken.stage=4;check(!failure.tick(),"failed public sample stops the experiment");}
        check(!failure.active()&&broken.calls.back()=="shutdown","command/sample failure removes reference");
    }
    Fake abandoned;Text log;Output out(log);
    {Session failure(abandoned,out);check(failure.start(true),"unwind fixture starts");}
    check(log.value.find("\"reason\":5")!=std::string::npos,"abandoned session is not a requested successful stop");
}
void budgets(){
    Fake api;Text text;Output output(text);Session session(api,output);check(session.start(true),"budget fixture starts");
    for(unsigned n=0;n<MaxSamples+20;++n)check(session.tick(),"logging budget does not hide reference");
    check(session.samples()==MaxSamples&&session.active()&&output.bytes()<MaxOutput,"sample and byte budgets are bounded");
    check(text.value.find("\"event\":\"budget\"")!=std::string::npos,"budget boundary is explicit");
    check(session.command("q")&&!session.active()&&text.value.find("\"event\":\"stopped\"")!=std::string::npos,"stop still works after logging budget");
    Fake other;Text broken;Output badOutput(broken);Session failure(other,badOutput);check(failure.start(true),"output fixture starts");
    broken.fail=true;check(!failure.tick()&&!failure.active()&&other.calls.back()=="shutdown","broken output triggers owned cleanup");
}
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string(argv[1])=="--transcript"){
        Fake api;Text text;Output output(text);Session session(api,output);
        if(!session.start(true))return 2;
        for(const auto* phase:{"still","yaw","move"}){
            session.command(phase);api.current.beginMicros+=100;api.current.endMicros+=100;
            api.current.angular[1]=.5f;api.current.intervalMillis=11.1f;api.current.markerVisible=true;session.tick();
        }
        session.stop();std::fputs(text.value.c_str(),stdout);return 0;
    }
    geometry();frozenReference();failures();budgets();std::printf("%u Frame-local world-reference checks PASS\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
