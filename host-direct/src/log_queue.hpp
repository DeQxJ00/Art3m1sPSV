#pragma once
#include <pthread.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

// Bounded diagnostics only. Producers never perform storage I/O or wait for
// queue capacity. Slow storage may drop logs, but cannot backpressure gameplay.
class LogQueue {
public:
    static constexpr unsigned Slots=32, LineSize=16384;
    using Sink=void(*)(const char*,size_t,bool);
    using Clock=uint64_t(*)();
    struct Stats { uint64_t dropped=0,truncated=0,maxWrite=0,maxFlush=0; unsigned queued=0; };
    bool start(Sink sink,Clock clock,void(*init)()=nullptr){
        sink_=sink;clock_=clock;init_=init;
        active_=pthread_create(&thread_,nullptr,entry,this)==0;return active_;
    }
    void append(const char* format,va_list args){
        pthread_mutex_lock(&mutex_);
        if(stop_ || !active_ || count_==Slots){++stats_.dropped;pthread_mutex_unlock(&mutex_);return;}
        auto& line=lines_[(head_+count_)%Slots];
        int n=std::vsnprintf(line.bytes,LineSize-1,format,args);
        if(n<0){++stats_.dropped;pthread_mutex_unlock(&mutex_);return;}
        if(n>=int(LineSize-1))++stats_.truncated;
        line.length=std::min(unsigned(n),LineSize-2);line.bytes[line.length++]='\n';
        ++count_;pthread_cond_signal(&wake_);pthread_mutex_unlock(&mutex_);
    }
    void flush(){pthread_mutex_lock(&mutex_);flush_=true;pthread_cond_signal(&wake_);pthread_mutex_unlock(&mutex_);}
    Stats stats(){pthread_mutex_lock(&mutex_);auto s=stats_;s.queued=count_;pthread_mutex_unlock(&mutex_);return s;}
    void stop(){
        if(!active_)return;
        pthread_mutex_lock(&mutex_);stop_=true;pthread_cond_signal(&wake_);pthread_mutex_unlock(&mutex_);
        pthread_join(thread_,nullptr);active_=false;
    }
    ~LogQueue(){stop();}
private:
    struct Line { char bytes[LineSize]; unsigned length=0; } lines_[Slots]{},drain_{};
    pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t wake_=PTHREAD_COND_INITIALIZER;
    pthread_t thread_{};
    Sink sink_=nullptr;Clock clock_=nullptr;void(*init_)()=nullptr;
    unsigned head_=0,count_=0;bool flush_=false,stop_=false,active_=false;Stats stats_{};
    static void* entry(void* p){static_cast<LogQueue*>(p)->run();return nullptr;}
    void run(){
        if(init_)init_();
        for(;;){
            pthread_mutex_lock(&mutex_);
            while(!count_&&!flush_&&!stop_)pthread_cond_wait(&wake_,&mutex_);
            bool have=count_!=0, flush=flush_, done=stop_&&!have;
            flush_=false;
            if(have){auto& line=lines_[head_];drain_.length=line.length;
                std::memcpy(drain_.bytes,line.bytes,line.length);head_=(head_+1)%Slots;--count_;}
            pthread_mutex_unlock(&mutex_);
            uint64_t write=0,flushed=0;
            if(have){auto at=clock_();sink_(drain_.bytes,drain_.length,false);write=clock_()-at;}
            if(flush||done){auto at=clock_();sink_(nullptr,0,true);flushed=clock_()-at;}
            pthread_mutex_lock(&mutex_);
            stats_.maxWrite=std::max(stats_.maxWrite,write);stats_.maxFlush=std::max(stats_.maxFlush,flushed);
            pthread_mutex_unlock(&mutex_);
            if(done)break;
        }
    }
};
