#include "status_cache.hpp"
#include <cassert>
#include <mutex>
#include <condition_variable>
#include <future>
#include <chrono>
#include <cstdio>
struct Stat { int version=0; };
static std::mutex mutex;
static std::condition_variable changed;
static int calls=0,version=1,result=-2;
static bool block=false,entered=false,release=false;
static int probe(const char*,Stat* stat){
    std::unique_lock<std::mutex> guard(mutex);++calls;
    if(block){entered=true;changed.notify_all();changed.wait(guard,[]{return release;});}
    stat->version=version;return result;
}
int main(){
    StatusCache<Stat,2> cache(probe);Stat stat;
    assert(cache.read("first",&stat)==-2&&stat.version==1&&calls==1);
    for(int i=0;i<1000;++i)assert(cache.read("first",&stat)==-2&&stat.version==1);
    assert(calls==1);
    {std::lock_guard<std::mutex> guard(mutex);block=true;version=2;result=0;}
    auto worker=std::async(std::launch::async,[&]{cache.refresh();});
    {std::unique_lock<std::mutex> guard(mutex);assert(changed.wait_for(guard,std::chrono::seconds(2),[]{return entered;}));}
    // A blocked filesystem refresh must not block main-thread snapshot reads.
    auto reader=std::async(std::launch::async,[&]{Stat s;return cache.read("first",&s)==-2&&s.version==1;});
    assert(reader.wait_for(std::chrono::seconds(2))==std::future_status::ready&&reader.get());
    {std::lock_guard<std::mutex> guard(mutex);release=true;changed.notify_all();}
    worker.get();assert(cache.read("first",&stat)==0&&stat.version==2);
    assert(cache.read("second",&stat)==0);const int before=calls;
    // Capacity overflow does not silently reuse a different path's status.
    assert(cache.read("uncached",&stat)==0&&cache.read("uncached",&stat)==0);assert(calls==before+2);
    {std::lock_guard<std::mutex> guard(mutex);result=-7;version=3;}
    cache.refresh();assert(cache.read("first",&stat)==-7&&stat.version==3);
    std::puts("PASS: initial status, cached reads, blocked refresh isolation, error transitions and bounded overflow");
}
