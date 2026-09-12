#include "../../host-direct/src/clock_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
static int mhz=333,limit=444;static bool locked=false;
static std::vector<int> writes;
static int get(){return mhz;}
static int set(int n){writes.push_back(n);if(n>limit)return -2;if(!locked)mhz=n;return 0;}
static int gpu=111,gpuLimit=222;static bool gpuLocked=false;static std::vector<int> gpuWrites;
static int get_gpu(){return gpu;}
static int set_gpu(int n){gpuWrites.push_back(n);if(n>gpuLimit)return -2;if(!gpuLocked)gpu=n;return 0;}
int main(){
    using namespace direct;ClockSettings v;
    for(int a:{0,444})for(int b:{0,444}){
        auto text="1 "+std::to_string(a)+" "+std::to_string(b)+"\n";
        assert(parse_clock_settings(text.c_str(),v)&&v.global==a&&v.ogv==b);
        assert(v.es4Global==0&&v.es4Ogv==0);
        assert(next_clock_choice(next_clock_choice(a,1),-1)==a);
    }
    for(int a:{0,111,166,222})for(int b:{0,111,166,222}){
        auto text="2 444 0 "+std::to_string(a)+" "+std::to_string(b);
        assert(parse_clock_settings(text.c_str(),v)&&v.global==444&&v.ogv==0&&v.es4Global==a&&v.es4Ogv==b);
        assert(next_es4_clock_choice(next_es4_clock_choice(a,1),-1)==a);
    }
    for(auto s:{"2 0 0 444 0","2 0 0 166 -1","2 0 0 166","2 0 0 166 222 extra","1 0 0 166 222"})assert(!parse_clock_settings(s,v));
    for(auto s:{"","1 333 500","1 444 -1","1 0 499","2 0 0","1 0","1 0 0 extra"})assert(!parse_clock_settings(s,v));
    ClockPolicy p{get,set};p.configure({});assert(writes.empty());
    p.configure({0,444});assert(writes.empty());p.video_active(true);assert(mhz==444);
    auto count=writes.size();p.video_active(true);assert(writes.size()==count);
    p.video_active(false);assert(mhz==333&&!p.owned);
    // External frequency changes while disabled become the next restore point.
    mhz=444;p.video_active(true);p.video_active(false);assert(mhz==444);
    mhz=333;p.configure({444,444});assert(mhz==444);
    p.video_active(true);assert(mhz==444);p.video_active(false);assert(mhz==444);
    p.shutdown();assert(mhz==333);
    // All lifecycle exits use the same close transition: EOF, skip, failure,
    // replacement with an MP4, and game destruction. Repeated close is harmless.
    for(int i=0;i<5;++i){p.configure({0,444});p.video_active(true);assert(mhz==444);
        p.video_active(false);assert(mhz==333);count=writes.size();p.video_active(false);assert(writes.size()==count);p.shutdown();}
    p.configure({444,0});p.video_active(true);assert(mhz==444);p.shutdown();assert(mhz==333);
    limit=333;p.configure({0,444});p.video_active(true);assert(mhz==333&&p.result<0);p.video_active(false);assert(mhz==333&&!p.owned);
    limit=444;locked=true;p.configure({444,0});assert(mhz==333&&p.result<0);p.shutdown();assert(!p.owned);locked=false;
    ClockPolicy g{get_gpu,set_gpu};g.configure({0,222});assert(gpuWrites.empty());
    // Independent ownership and restoration: changing GPU never writes CPU.
    p.configure({444,444});count=writes.size();g.video_active(true);assert(gpu==222&&writes.size()==count);
    g.video_active(false);assert(gpu==111&&mhz==444);
    g.configure({166,222});p.video_active(true);g.video_active(true);assert(gpu==222&&mhz==444);
    p.video_active(false);g.video_active(false);assert(gpu==166&&mhz==444);
    p.shutdown();g.shutdown();assert(gpu==111&&mhz==333);
    gpu=166;g.configure({0,222});g.video_active(true);g.video_active(false);assert(gpu==166);
    gpuLimit=166;g.video_active(true);assert(g.result<0&&gpu==166);g.video_active(false);assert(!g.owned);
    gpuLimit=222;gpuLocked=true;g.configure({222,0});assert(g.result<0&&gpu==166);g.shutdown();gpuLocked=false;
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-clock-settings-test";
    std::filesystem::create_directories(dir);const auto path=(dir/"cpu-clock.conf").string();
    std::filesystem::remove(path);std::filesystem::remove(path+".bak");
    assert(load_clock_settings(path,v)&&v.global==0&&v.ogv==0);
    assert(save_clock_settings(path,{444,444}));assert(load_clock_settings(path,v)&&v.global==444&&v.ogv==444);
    std::filesystem::rename(path,path+".bak");assert(load_clock_settings(path,v)&&v.ogv==444);
    assert(save_clock_settings(path,{0,444}));assert(load_clock_settings(path,v)&&v.global==0&&v.ogv==444);
    assert(save_clock_settings(path,{444,0,166,222}));assert(load_clock_settings(path,v)&&v.es4Global==166&&v.es4Ogv==222);
    {std::ofstream f(path);f<<"1 444 500\n";}
    assert(load_clock_settings(path,v)&&v.global==444&&v.ogv==0&&v.es4Global==0&&v.es4Ogv==0);
    {std::ofstream f(path);f<<"2 500 444 166 222\n";}
    assert(load_clock_settings(path,v)&&v.global==0&&v.ogv==444&&v.es4Global==166&&v.es4Ogv==222);
    assert(!save_clock_settings(path,{444,500,166,222}));
    assert(!save_clock_settings(path,{500,444,166,222}));
    assert(!save_clock_settings(path,{444,0,500,0}));
    assert(!save_clock_settings(path,{333,0}));
    {std::ofstream f(path);f<<"broken";}assert(!load_clock_settings(path,v)&&v.global==0&&v.ogv==0);
    {std::ofstream f(path);f<<"1 0 0"<<std::string(200,' ');}assert(!load_clock_settings(path,v));
    std::filesystem::remove(path);std::filesystem::remove(path+".bak");std::filesystem::remove(dir);
    std::cout<<"PASS clock choices, restore policy, repeated close, unsupported/locked clocks and persistence\n";
}
