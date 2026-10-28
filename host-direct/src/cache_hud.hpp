#pragma once
#include "hud_font.hpp"
#include "cache_hud_settings.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
extern "C" void art3m1s_cache_hud_enable(int);
extern "C" int art3m1s_cache_hud_snapshot(uint64_t*,size_t);
extern "C" int art3m1s_emote_source_cache_snapshot(uint64_t*,size_t);
extern "C" int art3m1s_ogv_cache_snapshot(uint64_t*,size_t);
namespace direct {
struct CacheHud : HudFont {
    bool active=false;uint64_t next=0;uint64_t values[24]{};
    uint64_t emote[11]{};uint64_t ogv[8]{};
    void prepare(bool enabled,uint64_t now){
        if(enabled!=active){active=enabled;art3m1s_cache_hud_enable(enabled);next=0;std::memset(values,0,sizeof(values));}
        if(!HudFont::prepare(active)){if(active){active=false;art3m1s_cache_hud_enable(0);}return;}
        if(now>=next){art3m1s_cache_hud_snapshot(values,24);art3m1s_emote_source_cache_snapshot(emote,11);art3m1s_ogv_cache_snapshot(ogv,8);next=now+500000;}
    }
    void draw()const{
        if(!active||!atlas)return;
        constexpr float x=660,y=12;const bool models=emote[0]||emote[2]||emote[3]||emote[6];const bool videos=ogv[0]||ogv[4]||ogv[5]||ogv[7];rect(x-8,y-8,300,(models?404:278)+(videos?72:0),0x07111fff);
        text(x,y,"CACHE MIB",0x7bdfb7ff);
        if(values[0]!=1){text(x,y+22,"WAITING FOR SAMPLE");return;}
        const auto mib=[](uint64_t n){return double(n)/1048576.;};char line[64];int row=1;
        const auto show=[&](const char* label,double used,double cap){std::snprintf(line,sizeof(line),"%s %.1f/%.1f",label,used,cap);text(x,y+row++*18,line);};
        show("TOTAL",mib(values[2]+values[5]+emote[0]+ogv[0]),mib(values[1]));
        std::snprintf(line,sizeof(line),"READY %.1f",mib(values[2]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"PIX %.1f ZIP %.1f",mib(values[3]),mib(values[4]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"LUA LOAD %llu/%llu",(unsigned long long)values[21],(unsigned long long)values[20]);text(x,y+row++*18,line,0x7bdfb7ff);
        std::snprintf(line,sizeof(line),"LUA PIX %llu ZIP %llu",(unsigned long long)values[22],(unsigned long long)values[23]);text(x,y+row++*18,line);
        show("IDLE",mib(values[5]),mib(values[6]));show("CPU",mib(values[7]),mib(values[10]));show("GPU",mib(values[8]),mib(values[11]));
        std::snprintf(line,sizeof(line),"IDLE ZIP %.1f",mib(values[9]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"HIT GPU %llu",(unsigned long long)values[12]);text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"CPU %llu ZIP %llu",(unsigned long long)values[13],(unsigned long long)values[14]);text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"MISS %llu EVICT %llu",(unsigned long long)values[15],(unsigned long long)values[16]);text(x,y+row++*18,line);
        if(models){
            std::snprintf(line,sizeof(line),"PSB MEMORY %.1f MIB",mib(emote[0]));text(x,y+row++*18,line,0x7bdfb7ff);
            std::snprintf(line,sizeof(line),"PSB CACHED %llu FILES",(unsigned long long)emote[1]);text(x,y+row++*18,line);
            std::snprintf(line,sizeof(line),"FILE REUSE %llu READ %llu",(unsigned long long)emote[2],(unsigned long long)emote[3]);text(x,y+row++*18,line);
            if(emote[6])std::snprintf(line,sizeof(line),"PRELOAD READY %llu/%llu",(unsigned long long)emote[7],(unsigned long long)emote[6]);
            else std::snprintf(line,sizeof(line),"PRELOAD: NO PLAN");
            text(x,y+row++*18,line);
            std::snprintf(line,sizeof(line),"PARSED LIVE %.1f MIB",mib(emote[8]));text(x,y+row++*18,line);
            std::snprintf(line,sizeof(line),"PARSE REUSE %llu RUN %llu",(unsigned long long)emote[9],(unsigned long long)emote[10]);text(x,y+row++*18,line);
            text(x,y+row++*18,"COUNTS SINCE GAME START",0xa5b6cfff);
        }
        if(videos){
            show("OGV",mib(ogv[0]),mib(ogv[1]));
            std::snprintf(line,sizeof(line),"OGV GROUP %llu/%llu",(unsigned long long)ogv[2],(unsigned long long)ogv[3]);text(x,y+row++*18,line,0x7bdfb7ff);
            std::snprintf(line,sizeof(line),"READY %llu LOAD %llu",(unsigned long long)ogv[6],(unsigned long long)ogv[7]);text(x,y+row++*18,line);
            std::snprintf(line,sizeof(line),"HIT %llu MISS %llu",(unsigned long long)ogv[4],(unsigned long long)ogv[5]);text(x,y+row++*18,line);
        }
        text(x,y+row++*18,"SHARED CACHE ONLY",0xa5b6cfff);
    }
};
}
