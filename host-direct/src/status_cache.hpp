#pragma once
#include <pthread.h>
#include <cstring>

// Fixed diagnostic paths only. First use observes storage synchronously;
// subsequent reads consume a snapshot while a worker refreshes it. No I/O is
// performed under the registry lock, including a cache miss or refresh.
template<class Stat,unsigned Capacity=32> class StatusCache {
public:
    using Probe=int(*)(const char*,Stat*);
    explicit StatusCache(Probe probe):probe_(probe){}
    int read(const char* path,Stat* stat){
        pthread_mutex_lock(&mutex_);
        for(unsigned i=0;i<count_;++i)if(std::strcmp(entries_[i].path,path)==0){
            const auto e=entries_[i];pthread_mutex_unlock(&mutex_);*stat=e.stat;return e.result;
        }
        pthread_mutex_unlock(&mutex_);
        Stat value{};const int result=probe_(path,&value);
        pthread_mutex_lock(&mutex_);
        // Another initial reader may have registered/refreshed this path while
        // our probe was in flight. Do not overwrite its newer snapshot.
        for(unsigned i=0;i<count_;++i)if(std::strcmp(entries_[i].path,path)==0){
            const auto e=entries_[i];pthread_mutex_unlock(&mutex_);*stat=e.stat;return e.result;
        }
        if(count_<Capacity&&std::strlen(path)<sizeof(entries_[0].path)){
            auto& e=entries_[count_++];std::strcpy(e.path,path);e.stat=value;e.result=result;
        }
        pthread_mutex_unlock(&mutex_);*stat=value;return result;
    }
    // Single worker owns refresh; registration and snapshot readers can run
    // concurrently. Newly registered entries are picked up on the next pass.
    void refresh(){
        pthread_mutex_lock(&mutex_);const unsigned count=count_;pthread_mutex_unlock(&mutex_);
        for(unsigned i=0;i<count;++i){
            char path[128];
            pthread_mutex_lock(&mutex_);std::strcpy(path,entries_[i].path);pthread_mutex_unlock(&mutex_);
            Stat value{};const int result=probe_(path,&value);
            pthread_mutex_lock(&mutex_);entries_[i].stat=value;entries_[i].result=result;pthread_mutex_unlock(&mutex_);
        }
    }
private:
    struct Entry { char path[128]{};Stat stat{};int result=0; } entries_[Capacity];
    Probe probe_;unsigned count_=0;pthread_mutex_t mutex_=PTHREAD_MUTEX_INITIALIZER;
};
