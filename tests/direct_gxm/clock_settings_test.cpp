#include "../../host-direct/src/clock_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
static int mhz=333,limit=500;static bool locked=false;
static std::vector<int> writes;
static int get(){return mhz;}
static int set(int n){writes.push_back(n);if(n>limit)return -2;if(!locked)mhz=n;return 0;}
int main(){
    using namespace direct;ClockSettings v;
    for(int a:{0,444,500})for(int b:{0,444,500}){
        auto text="1 "+std::to_string(a)+" "+std::to_string(b)+"\n";
        assert(parse_clock_settings(text.c_str(),v)&&v.global==a&&v.ogv==b);
        assert(next_clock_choice(next_clock_choice(a,1),-1)==a);
    }
    for(auto s:{"","1 333 500","1 444 -1","1 0 499","2 0 0","1 0","1 0 0 extra"})assert(!parse_clock_settings(s,v));
    CpuClockPolicy p{get,set};p.configure({});assert(writes.empty());
    p.configure({0,500});assert(writes.empty());p.video_active(true);assert(mhz==500);
    auto count=writes.size();p.video_active(true);assert(writes.size()==count);
    p.video_active(false);assert(mhz==333&&!p.owned);
    // External frequency changes while disabled become the next restore point.
    mhz=444;p.video_active(true);p.video_active(false);assert(mhz==444);
    mhz=333;p.configure({444,500});assert(mhz==444);
    p.video_active(true);assert(mhz==500);p.video_active(false);assert(mhz==444);
    p.shutdown();assert(mhz==333);
    // All lifecycle exits use the same close transition: EOF, skip, failure,
    // replacement with an MP4, and game destruction. Repeated close is harmless.
    for(int i=0;i<5;++i){p.configure({444,500});p.video_active(true);assert(mhz==500);
        p.video_active(false);assert(mhz==444);count=writes.size();p.video_active(false);assert(writes.size()==count);p.shutdown();}
    p.configure({500,444});p.video_active(true);assert(mhz==444);p.video_active(false);assert(mhz==500);p.shutdown();assert(mhz==333);
    p.configure({444,0});p.video_active(true);assert(mhz==444);p.shutdown();assert(mhz==333);
    limit=444;p.configure({0,500});p.video_active(true);assert(mhz==333&&p.result<0);p.video_active(false);assert(mhz==333&&!p.owned);
    limit=500;locked=true;p.configure({444,0});assert(mhz==333&&p.result<0);p.shutdown();assert(!p.owned);locked=false;
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-clock-settings-test";
    std::filesystem::create_directories(dir);const auto path=(dir/"cpu-clock.conf").string();
    std::filesystem::remove(path);std::filesystem::remove(path+".bak");
    assert(load_clock_settings(path,v)&&v.global==0&&v.ogv==0);
    assert(save_clock_settings(path,{444,500}));assert(load_clock_settings(path,v)&&v.global==444&&v.ogv==500);
    std::filesystem::rename(path,path+".bak");assert(load_clock_settings(path,v)&&v.ogv==500);
    assert(save_clock_settings(path,{0,444}));assert(load_clock_settings(path,v)&&v.global==0&&v.ogv==444);
    assert(!save_clock_settings(path,{333,500}));
    {std::ofstream f(path);f<<"broken";}assert(!load_clock_settings(path,v)&&v.global==0&&v.ogv==0);
    {std::ofstream f(path);f<<"1 0 0"<<std::string(200,' ');}assert(!load_clock_settings(path,v));
    std::filesystem::remove(path);std::filesystem::remove(path+".bak");std::filesystem::remove(dir);
    std::cout<<"PASS clock choices, restore policy, repeated close, unsupported/locked clocks and persistence\n";
}
