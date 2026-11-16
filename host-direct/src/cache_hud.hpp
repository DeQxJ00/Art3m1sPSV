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
    bool active=false;uint64_t next=0;uint64_t values[27]{};
    PerformanceMemory memory{};
    uint64_t emote[12]{};uint64_t ogv[8]{};
    void prepare(bool enabled,uint64_t now){
        if(enabled!=active){active=enabled;art3m1s_cache_hud_enable(enabled);next=0;std::memset(values,0,sizeof(values));}
        if(!HudFont::prepare(active)){if(active){active=false;art3m1s_cache_hud_enable(0);}return;}
        if(now>=next){memory=performance_memory_snapshot();art3m1s_cache_hud_snapshot(values,27);art3m1s_emote_source_cache_snapshot(emote,12);art3m1s_ogv_cache_snapshot(ogv,8);next=now+500000;}
    }
    void draw()const{
        if(!active||!atlas)return;
        constexpr float x=636,y=12,right=948,step=16;
        const bool models=emote[0]||emote[2]||emote[3]||emote[6];
        const bool videos=ogv[0]||ogv[4]||ogv[5]||ogv[7];
        const auto mib=[](uint64_t n){return double(n)/1048576.;};
        struct Row {char text[128];uint32_t color;};
        Row rows[40]{};unsigned count=0;
        const auto add=[&](uint32_t color,const char* format,auto... args){
            if(count>=40)return;
            auto& r=rows[count++];r.color=color;std::snprintf(r.text,sizeof(r.text),format,args...);
        };
        constexpr uint32_t white=0xe8f0ffff,green=0x7bdfb7ff;
        add(green,"DEBUG CACHE / MEMORY - MIB");
        if(values[0]!=1)add(white,"WAITING FOR SAMPLE");
        else{
            add(white,"CACHE %.1f/%.0f  READY %.1f",mib(values[2]+values[5]+emote[0]+ogv[0]),mib(values[1]),mib(values[2]));
            add(white,"READY PIX %.1f  ZIP %.1f",mib(values[3]),mib(values[4]));
            add(green,"BIND %llu  MISSING %llu",(unsigned long long)values[24],(unsigned long long)values[26]);
            add(white,"LUA WAIT %llu  READY %llu",(unsigned long long)values[25],(unsigned long long)(values[22]+values[23]));
            add(white,"LUA PIX %llu  ZIP %llu",(unsigned long long)values[22],(unsigned long long)values[23]);
            add(white,"IDLE %.1f/%.0f  ZIP %.1f",mib(values[5]),mib(values[6]),mib(values[9]));
            add(white,"CPU %.1f/%.1f  GPU %.1f/%.0f",mib(values[7]),mib(values[10]),mib(values[8]),mib(values[11]));
            add(white,"HIT GPU %llu CPU %llu ZIP %llu",(unsigned long long)values[12],(unsigned long long)values[13],(unsigned long long)values[14]);
            add(white,"MISS %llu  EVICT %llu",(unsigned long long)values[15],(unsigned long long)values[16]);
        }
        if(models){
            add(green,"PSB MEMORY %.1f/%.0f  FILES %llu",mib(emote[0]),mib(emote[11]),(unsigned long long)emote[1]);
            add(white,"PSB REUSE %llu  READ %llu",(unsigned long long)emote[2],(unsigned long long)emote[3]);
            if(emote[6])add(white,"PRELOAD READY %llu/%llu",(unsigned long long)emote[7],(unsigned long long)emote[6]);
            else add(white,"PRELOAD NO PLAN");
            add(white,"PARSED LIVE %.1f",mib(emote[8]));
            add(white,"PARSE REUSE %llu  RUN %llu",(unsigned long long)emote[9],(unsigned long long)emote[10]);
        }
        if(videos){
            add(green,"OGV %.1f/%.0f  GROUP %llu/%llu",mib(ogv[0]),mib(ogv[1]),(unsigned long long)ogv[2],(unsigned long long)ogv[3]);
            add(white,"OGV READY %llu  LOAD %llu",(unsigned long long)ogv[6],(unsigned long long)ogv[7]);
            add(white,"OGV HIT %llu  MISS %llu",(unsigned long long)ogv[4],(unsigned long long)ogv[5]);
        }
        add(green,"----------------------------------");
        if(memory.heapValid){
            add(white,"HEAP USED %.1f  FREE %.1f",mib(memory.heapUsed),mib(memory.heapFree));
            add(white,"     ARENA %.1f",mib(memory.heapArena));
        }else add(white,"HEAP N/A");
        if(memory.ledgerValid){
            const auto* v=memory.ledger;
            if(memory.heapOtherValid)add(white,"TRACKED HEAP %.1f  OTHER %.1f",mib(v[4]),mib(memory.heapOther));
            else add(white,"TRACKED HEAP %.1f  OTHER N/A",mib(v[4]));
            add(white,"CDRAM %.1f  UNCACHED %.1f",mib(v[41]),mib(v[78]));
            add(white,"RETIRED %.1f",mib(v[6]+v[43]+v[80]));
            const auto owner=[&](unsigned o){return v[8+o*3]+v[45+o*3]+v[82+o*3];};
            add(white,"IMAGE %.1f  TEXTURE %.1f",mib(owner(0)+owner(1)+owner(2)+owner(3)+owner(7)),mib(owner(4)));
            add(white,"FIXED/RT %.1f  FONT %.1f",mib(owner(5)+owner(6)),mib(owner(9)));
            add(white,"MEDIA/AUDIO %.1f",mib(owner(8)+owner(10)));
        }else add(white,"TRACKED MEMORY N/A");
        char ram[24],vram[24];
        if(memory.ramFreeValid)std::snprintf(ram,sizeof(ram),"%.1f",mib(memory.ramFree));else std::strcpy(ram,"N/A");
        if(memory.vramFreeValid)std::snprintf(vram,sizeof(vram),"%.1f",mib(memory.vramFree));else std::strcpy(vram,"N/A");
        add(white,"FREE RAM %s  CDRAM %s",ram,vram);
        rect(x-8,y-8,324,count*step+14,0x07111fff);
        for(unsigned i=0;i<count;++i){
            // Keep large counters inside the panel without truncating digits.
            const size_t length=std::strlen(rows[i].text);
            const float fit=length? (right-x)/(length*6.f):1.5f;
            text(x,y+i*step,rows[i].text,rows[i].color,right,fit<1.5f?fit:1.5f);
        }
    }
};
}
