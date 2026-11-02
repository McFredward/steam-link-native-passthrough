// Minimize the dashboard only when the user enters capture or corner placement.
// The native command runs outside the renderer and never restarts a service.
#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char** environ;

namespace room_native {
class ScanViewTransition {
    std::string page_;bool collecting_=false;
public:
    bool observe(const nlohmann::json& state){
        const auto page=state.value("page","");const bool collecting=state.value("capture_collecting",false);
        const bool entered=page!=page_;
        const bool hide=(entered&&(page=="place"||page=="align_place"))
            ||(page=="scan"&&collecting&&(entered||!collecting_));
        page_=page;collecting_=collecting;return hide;
    }
    void reset(){page_.clear();collecting_=false;}
};
class ScanDashboardHider {
    std::string program_;std::mutex mutex_;std::condition_variable condition_;
    std::atomic<bool> stop_{false},cancelledCompletion_{false};std::atomic<unsigned> epoch_{0};unsigned request_=0;
    std::thread worker_;
    bool run(){
        char canonical[4096]{};struct stat info{};
        if(!realpath(program_.c_str(),canonical)||std::filesystem::path(canonical).filename()!="vrcmd"
            ||stat(canonical,&info)||!S_ISREG(info.st_mode)||(info.st_mode&0022)
            ||(info.st_uid!=0&&info.st_uid!=getuid())||!(info.st_mode&0111))return false;
        char argument[]="--hidedashboard";char* args[]{canonical,argument,nullptr};pid_t child=-1;
        if(posix_spawn(&child,canonical,nullptr,nullptr,args,environ))return false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        for(;;){int status=0;const auto result=waitpid(child,&status,WNOHANG);
            if(result==child)return WIFEXITED(status)&&WEXITSTATUS(status)==0;
            if(result<0){if(errno==EINTR)continue;return false;}
            if(stop_||std::chrono::steady_clock::now()>deadline){
                // Only this unreaped child can own its PID. No native service
                // is signalled; the command is an ordinary short-lived client.
                kill(child,SIGKILL);waitpid(child,&status,0);return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void loop(){
        while(!stop_){unsigned token=0;{
            std::unique_lock<std::mutex> lock(mutex_);condition_.wait(lock,[&]{return stop_||request_!=0;});
            if(stop_)return;
            token=request_;request_=0;
        }
        if(token!=epoch_)continue;
        const bool ok=run();
        if(token!=epoch_)cancelledCompletion_=true;
        std::fprintf(stderr,"Room view: immersive dashboard command %s\n",ok?"completed":"unavailable");
        }
    }
public:
    explicit ScanDashboardHider(const std::string& library):program_((std::filesystem::path(library).parent_path()/"vrcmd").string()),worker_([this]{loop();}){}
    ~ScanDashboardHider(){stop_=true;condition_.notify_all();if(worker_.joinable())worker_.join();}
    void request(){const auto token=++epoch_;std::lock_guard<std::mutex> lock(mutex_);request_=token;condition_.notify_one();}
    void cancel(){++epoch_;std::lock_guard<std::mutex> lock(mutex_);request_=0;}
    bool cancelledCompletion(){return cancelledCompletion_.exchange(false);}
};
} // namespace room_native
