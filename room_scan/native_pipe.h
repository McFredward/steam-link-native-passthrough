#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <queue>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace room_native {
class Backend {
public:
    std::atomic<bool> alive{false};
    bool start(const std::string& root,const std::string& data){
        int incoming[2]{},outgoing[2]{};
        if(pipe2(incoming,O_CLOEXEC))return false;
        if(pipe2(outgoing,O_CLOEXEC)){close(incoming[0]);close(incoming[1]);return false;}
        child_=fork();
        if(child_==0){
            dup2(incoming[0],STDIN_FILENO);dup2(outgoing[1],STDOUT_FILENO);
            for(int fd:{incoming[0],incoming[1],outgoing[0],outgoing[1]})close(fd);
            if(chdir(root.c_str()))_exit(126);
            execl("/usr/bin/python3","python3","-B","-m","room_scan.app",data.c_str(),static_cast<char*>(nullptr));_exit(127);
        }
        close(incoming[0]);close(outgoing[1]);
        if(child_<0){close(incoming[1]);close(outgoing[0]);return false;}
        input_=incoming[1];output_=outgoing[0];fcntl(input_,F_SETFL,fcntl(input_,F_GETFL)|O_NONBLOCK);
        alive=true;writer_=std::thread([this]{writeLoop();});reader_=std::thread([this]{readLoop();});return true;
    }
    bool send(const nlohmann::json& event,bool observation=false){
        if(!alive)return false;
        const auto line=event.dump()+"\n";if(line.size()>1024*1024)return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(queue_.size()>=(observation?2u:64u)||queuedBytes_+line.size()>4*1024*1024)return false;
        queuedBytes_+=line.size();queue_.push(line);condition_.notify_one();return true;
    }
    std::shared_ptr<const nlohmann::json> latest()const{return std::atomic_load(&latest_);}
    ~Backend(){stop();}
    void stop(){
        stop_=true;condition_.notify_all();if(writer_.joinable())writer_.join();
        if(child_>0){bool exited=false;for(unsigned n=0;n<40;++n){int status=0;if(waitpid(child_,&status,WNOHANG)==child_){exited=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(50));}
            if(!exited){kill(child_,SIGTERM);waitpid(child_,nullptr,0);}child_=-1;}
        if(reader_.joinable())reader_.join();
        if(output_>=0){close(output_);output_=-1;}alive=false;
    }
private:
    pid_t child_=-1;int input_=-1,output_=-1;std::atomic<bool> stop_{false};
    std::thread reader_,writer_;std::mutex mutex_;std::condition_variable condition_;
    std::queue<std::string> queue_;std::size_t queuedBytes_=0;
    std::shared_ptr<const nlohmann::json> latest_;
    void writeLoop(){
        while(!stop_){std::string line;{
                std::unique_lock<std::mutex> lock(mutex_);condition_.wait(lock,[this]{return stop_||!queue_.empty();});if(stop_)break;
                line=std::move(queue_.front());queue_.pop();queuedBytes_-=line.size();}
            std::size_t offset=0;while(offset<line.size()&&!stop_){const auto result=write(input_,line.data()+offset,line.size()-offset);
                if(result>0){offset+=std::size_t(result);continue;}
                if(result<0&&(errno==EAGAIN||errno==EINTR)){pollfd fd{input_,POLLOUT,0};poll(&fd,1,50);continue;}
                alive=false;break;}
            if(!alive)break;
        }
        close(input_);input_=-1;
    }
    void readLoop(){
        std::string pending;std::array<char,16384> buffer{};
        while(!stop_){const auto n=read(output_,buffer.data(),buffer.size());if(n<0&&errno==EINTR)continue;if(n<=0)break;
            pending.append(buffer.data(),std::size_t(n));if(pending.size()>16*1024*1024)break;
            for(;;){const auto end=pending.find('\n');if(end==std::string::npos)break;
                try{auto data=std::make_shared<const nlohmann::json>(nlohmann::json::parse(pending.begin(),pending.begin()+std::ptrdiff_t(end)));
                    if(data->value("schema","")!="sfnp-room-dashboard-v1")throw std::runtime_error("Unknown dashboard state");
                    std::atomic_store(&latest_,std::move(data));}
                catch(const std::exception&){alive=false;return;}
                pending.erase(0,end+1);
            }
        }
        alive=false;
    }
};
} // namespace room_native
