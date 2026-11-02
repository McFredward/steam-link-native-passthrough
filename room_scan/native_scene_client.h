// Own one ordinary renderer child. Control cannot queue behind a mesh snapshot.
#pragma once
#include "native_scene_protocol.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace room_native {
inline bool trustedSceneExecutable(const std::string& path){
    const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat info{};Elf64_Ehdr elf{};
    const bool valid=fd>=0&&!fstat(fd,&info)&&S_ISREG(info.st_mode)&&!(info.st_mode&0022)
        &&(info.st_uid==getuid()||info.st_uid==0)&&(info.st_mode&0111)&&info.st_size>=64&&info.st_size<=8*1024*1024
        &&pread(fd,&elf,sizeof(elf),0)==ssize_t(sizeof(elf))&&!std::memcmp(elf.e_ident,ELFMAG,SELFMAG)
        &&elf.e_ident[EI_CLASS]==ELFCLASS64&&elf.e_ident[EI_DATA]==ELFDATA2LSB
#if defined(__aarch64__)
        &&elf.e_machine==EM_AARCH64;
#else
        &&elf.e_machine==EM_X86_64;
#endif
    if(fd>=0)close(fd);
    return valid;
}
class RoomSceneClient {
    pid_t child_=-1;int input_=-1,control_=-1;std::thread writer_;
    std::atomic<bool> stopped_{false},writeFailed_{false};std::mutex mutex_;std::condition_variable condition_;
    std::shared_ptr<const nlohmann::json> pending_;
    bool stopping_=false,termSent_=false;std::chrono::steady_clock::time_point stopAt_{};
    void writeLoop(){
        sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);pthread_sigmask(SIG_BLOCK,&blocked,nullptr);
        while(!stopped_){std::shared_ptr<const nlohmann::json> state;{
            std::unique_lock<std::mutex> lock(mutex_);condition_.wait(lock,[&]{return stopped_||pending_!=nullptr;});
            if(stopped_)break;
            state=std::move(pending_);
        }
        try{const auto line=state->dump()+"\n";if(line.size()>16*1024*1024)throw std::runtime_error("Room scene snapshot exceeds budget");
            std::size_t offset=0;while(offset<line.size()&&!stopped_){
                const auto written=write(input_,line.data()+offset,line.size()-offset);
                if(written>0){offset+=std::size_t(written);continue;}
                if(written<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)){pollfd fd{input_,POLLOUT,0};poll(&fd,1,50);continue;}
                throw std::runtime_error("Room scene snapshot disconnected");
            }
        }catch(const std::exception&){writeFailed_=true;break;}
        }
        close(input_);input_=-1;
    }
    void clear(){
        stopped_=true;condition_.notify_all();if(writer_.joinable())writer_.join();
        if(control_>=0){close(control_);control_=-1;}
        std::lock_guard<std::mutex> lock(mutex_);pending_.reset();
    }
public:
    bool running()const{return child_>0;}
    bool ownsPid(std::uint32_t pid)const{return child_>0&&pid==std::uint32_t(child_);}
    bool stopping()const{return stopping_;}
    bool writeFailed()const{return writeFailed_;}
    bool start(const std::string& executable,const std::string& loader){
        if(running()||!trustedSceneExecutable(executable))return false;
        int incoming[2]{-1,-1},sockets[2]{-1,-1};
        if(pipe2(incoming,O_CLOEXEC))return false;
        if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0,sockets)){
            close(incoming[0]);close(incoming[1]);return false;}
        constexpr int ChildControl=198;
        child_=fork();
        if(child_==0){
            if(dup2(incoming[0],STDIN_FILENO)<0||dup2(sockets[1],ChildControl)<0)_exit(126);
            for(int fd:{incoming[0],incoming[1],sockets[0],sockets[1]})if(fd!=STDIN_FILENO&&fd!=ChildControl)close(fd);
            execl(executable.c_str(),executable.c_str(),"--openxr",loader.c_str(),"--control-fd","198",static_cast<char*>(nullptr));_exit(127);
        }
        close(incoming[0]);close(sockets[1]);
        if(child_<0){close(incoming[1]);close(sockets[0]);return false;}
        input_=incoming[1];control_=sockets[0];fcntl(input_,F_SETFL,fcntl(input_,F_GETFL)|O_NONBLOCK);
        stopped_=false;writeFailed_=false;stopping_=termSent_=false;
        writer_=std::thread([this]{writeLoop();});return true;
    }
    void snapshot(std::shared_ptr<const nlohmann::json> state){
        if(!running()||stopping_)return;
        std::lock_guard<std::mutex> lock(mutex_);pending_=std::move(state);condition_.notify_one();
    }
    bool control(nlohmann::json value){return running()&&!stopping_&&sendScenePacket(control_,std::move(value));}
    std::optional<nlohmann::json> receive(){if(control_<0)return {};return receiveScenePacket(control_);}
    void requestStop(){
        if(!running()||stopping_)return;
        stopping_=true;stopAt_=std::chrono::steady_clock::now();
        try{sendScenePacket(control_,{{"event","stop"}});}catch(const std::exception&){}
        stopped_=true;condition_.notify_all();
    }
    std::optional<int> pollExit(){
        if(!running())return {};
        int status=0;const auto result=waitpid(child_,&status,WNOHANG);
        if(result==child_||(result<0&&errno==ECHILD)){child_=-1;clear();return status;}
        if(result<0&&errno!=EINTR)throw std::runtime_error("Owned room renderer wait failed");
        if(stopping_){const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-stopAt_).count();
            // This PID is our unreaped child. No native service is signalled.
            if(elapsed>5&&!termSent_){kill(child_,SIGTERM);termSent_=true;}
            if(elapsed>8)kill(child_,SIGKILL);
        }
        return {};
    }
    ~RoomSceneClient(){
        requestStop();
        for(unsigned n=0;running()&&n<180;++n){pollExit();if(running())std::this_thread::sleep_for(std::chrono::milliseconds(50));}
        if(running()){kill(child_,SIGKILL);int status=0;while(waitpid(child_,&status,0)<0&&errno==EINTR){}child_=-1;}
        clear();
    }
};
} // namespace room_native
