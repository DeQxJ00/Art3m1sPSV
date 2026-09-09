#include "log_queue.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <future>
#include <vector>
static std::mutex lock;
static std::condition_variable changed;
static bool entered=false,released=false;
static unsigned flushes=0;
static std::vector<std::string> output;
static uint64_t now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static void sink(const char* text,size_t size,bool flush){
    std::unique_lock<std::mutex> guard(lock);
    if(flush){++flushes;return;}
    if(!entered){entered=true;changed.notify_all();changed.wait(guard,[]{return released;});}
    output.emplace_back(text,size);
}
static void log(LogQueue& q,const char* fmt,...){va_list ap;va_start(ap,fmt);q.append(fmt,ap);va_end(ap);}
int main(){
    // Block storage indefinitely. Producers must complete independently, fill
    // exactly the bounded queue, and report overflow rather than waiting.
    static LogQueue q;
    assert(q.start(sink,now));log(q,"first");
    {std::unique_lock<std::mutex> g(lock);assert(changed.wait_for(g,std::chrono::seconds(2),[]{return entered;}));}
    auto producer=std::async(std::launch::async,[&]{
        std::string longLine(LogQueue::LineSize*2,'x');log(q,"%s",longLine.c_str());
        for(unsigned i=1;i<LogQueue::Slots+5;++i)log(q,"line %u",i);
        q.flush();return q.stats();
    });
    assert(producer.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    auto s=producer.get();assert(s.queued==LogQueue::Slots&&s.dropped==5&&s.truncated==1);
    {std::lock_guard<std::mutex> g(lock);released=true;changed.notify_all();}
    q.stop();q.stop();
    assert(output.size()==LogQueue::Slots+1&&output[0]=="first\n");
    assert(output[1].size()==LogQueue::LineSize-1&&output[1].back()=='\n');
    for(unsigned i=1;i<LogQueue::Slots;++i)assert(output[i+1]=="line "+std::to_string(i)+"\n");
    assert(flushes>=1);
    log(q,"after stop");assert(q.stats().dropped==6);
    std::puts("PASS: blocked sink, bounded overflow, truncation, FIFO, drain and repeated stop");
}
