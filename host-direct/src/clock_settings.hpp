#pragma once
#include <cstdio>
#include <cerrno>
#include <string>

namespace direct {
struct ClockSettings { int global=0,ogv=0; };
inline bool clock_choice(int mhz){return mhz==0||mhz==444||mhz==500;}
inline int next_clock_choice(int mhz,int step){
    const int values[]={0,444,500};int i=mhz==444?1:mhz==500?2:0;
    return values[(i+(step<0?2:1))%3];
}
inline bool parse_clock_settings(const char* text,ClockSettings& out){
    int version,global,ogv;char extra;
    if(std::sscanf(text,"%d %d %d %c",&version,&global,&ogv,&extra)!=3||
        version!=1||!clock_choice(global)||!clock_choice(ogv))return false;
    out={global,ogv};return true;
}
inline bool load_clock_settings(const std::string& path,ClockSettings& out){
    out={};FILE* f=std::fopen(path.c_str(),"rb");
    if(!f){if(errno!=ENOENT)return false;f=std::fopen((path+".bak").c_str(),"rb");
        if(!f)return errno==ENOENT;}
    char text[128]{};auto n=std::fread(text,1,sizeof(text)-1,f);
    bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
    return ok&&parse_clock_settings(text,out);
}
inline bool save_clock_settings(const std::string& path,const ClockSettings& value){
    if(!clock_choice(value.global)||!clock_choice(value.ogv))return false;
    const auto tmp=path+".tmp",bak=path+".bak";
    FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"1 %d %d\n",value.global,value.ogv)>0;
    if(std::fclose(f)!=0)ok=false;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}

// Main-thread policy. No polling, affinity changes or GPU/bus clock writes.
// Capture the external clock when taking ownership, and restore on release.
struct CpuClockPolicy {
    int (*get)();int (*set)(int);
    ClockSettings settings{};bool video=false,owned=false;
    int baseline=0,requested=0,actual=0,result=0;
    void apply(){
        int target=video&&settings.ogv?settings.ogv:settings.global;
        actual=get();result=0;
        if(!target&&!owned){requested=0;return;}
        if(!owned){if(actual<=0){result=-1;return;}baseline=actual;owned=true;}
        requested=target?target:baseline;
        if(actual!=requested){result=set(requested);actual=get();}
        // A plugin can return success while enforcing its own manual clock.
        if(result>=0&&actual!=requested)result=-1;
        if(!target&&result>=0)owned=false;
    }
    void configure(ClockSettings value){settings=value;apply();}
    bool video_active(bool on){if(video==on)return false;video=on;apply();return true;}
    void shutdown(){settings={};video=false;apply();}
};
}
