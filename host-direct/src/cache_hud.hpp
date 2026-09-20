#pragma once
#include "gpu.hpp"
#include "cache_hud_settings.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <vector>
extern "C" void art3m1s_cache_hud_enable(int);
extern "C" int art3m1s_cache_hud_snapshot(uint64_t*,size_t);
namespace direct {
struct CacheHud {
    Texture* atlas=nullptr;bool active=false;uint64_t next=0;uint64_t values[24]{};
    // Five column, seven row bitmap alphabet; no TTF/OTF allocation or font I/O.
    static constexpr char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ./:- ";
    static constexpr uint8_t columns[][5]={
        {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
        {39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
        {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
        {127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
        {127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
        {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
        {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},
        {97,81,73,69,67},{0,96,96,0,0},{32,16,8,4,2},{0,54,54,0,0},{8,8,8,8,8},{0,0,0,0,0}};
    void prepare(bool enabled,uint64_t now){
        if(enabled!=active){active=enabled;art3m1s_cache_hud_enable(enabled);next=0;std::memset(values,0,sizeof(values));}
        if(!active){if(atlas){destroy(atlas);atlas=nullptr;}return;}
        if(!atlas){std::vector<uint8_t> pixels(64*64*4,255);
            for(size_t i=3;i<pixels.size();i+=4)pixels[i]=0;
            for(unsigned i=0;i<sizeof(columns)/sizeof(columns[0]);++i)
                for(unsigned x=0;x<5;++x)for(unsigned y=0;y<7;++y)
                    if(columns[i][x]&(1<<y))pixels[(((i/8)*8+y)*64+(i%8)*8+x)*4+3]=255;
            atlas=texture(64,64,pixels.data());if(!atlas){active=false;art3m1s_cache_hud_enable(0);return;}}
        if(now>=next){art3m1s_cache_hud_snapshot(values,24);next=now+500000;}
    }
    void text(float x,float y,const char* s,uint32_t color=0xe8f0ffff)const{
        for(;*s&&x+10<=952;++s,x+=12){const char* k=std::strchr(alphabet,*s);if(!k)continue;unsigned i=unsigned(k-alphabet);
            float u=float(i%8)*8/64,v=float(i/8)*8/64;
            float r=(color>>24)/255.f,g=((color>>16)&255)/255.f,b=((color>>8)&255)/255.f;
            Vertex q[]={{x,y,u,v,r,g,b,1},{x+10,y,u+5.f/64,v,r,g,b,1},
                {x,y+14,u,v+7.f/64,r,g,b,1},{x+10,y+14,u+5.f/64,v+7.f/64,r,g,b,1}};
            draw_quad(atlas,q);
        }
    }
    void draw()const{
        if(!active||!atlas)return;
        constexpr float x=660,y=12;rect(x-8,y-8,300,278,0x07111fff);
        text(x,y,"CACHE MIB",0x7bdfb7ff);
        if(values[0]!=1){text(x,y+22,"WAITING FOR SAMPLE");return;}
        const auto mib=[](uint64_t n){return double(n)/1048576.;};char line[64];int row=1;
        const auto show=[&](const char* label,double used,double cap){std::snprintf(line,sizeof(line),"%s %.1f/%.1f",label,used,cap);text(x,y+row++*18,line);};
        show("TOTAL",mib(values[2]+values[5]),mib(values[1]));
        std::snprintf(line,sizeof(line),"READY %.1f",mib(values[2]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"PIX %.1f ZIP %.1f",mib(values[3]),mib(values[4]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"LUA LOAD %llu/%llu",(unsigned long long)values[21],(unsigned long long)values[20]);text(x,y+row++*18,line,0x7bdfb7ff);
        std::snprintf(line,sizeof(line),"LUA PIX %llu ZIP %llu",(unsigned long long)values[22],(unsigned long long)values[23]);text(x,y+row++*18,line);
        show("IDLE",mib(values[5]),mib(values[6]));show("CPU",mib(values[7]),mib(values[10]));show("GPU",mib(values[8]),mib(values[11]));
        std::snprintf(line,sizeof(line),"IDLE ZIP %.1f",mib(values[9]));text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"HIT GPU %llu",(unsigned long long)values[12]);text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"CPU %llu ZIP %llu",(unsigned long long)values[13],(unsigned long long)values[14]);text(x,y+row++*18,line);
        std::snprintf(line,sizeof(line),"MISS %llu EVICT %llu",(unsigned long long)values[15],(unsigned long long)values[16]);text(x,y+row++*18,line);
        text(x,y+row++*18,"SHARED CACHE ONLY",0xa5b6cfff);
    }
};
}
