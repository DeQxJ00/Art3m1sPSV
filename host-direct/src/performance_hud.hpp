#pragma once
#include "hud_font.hpp"
#include "performance_hud_settings.hpp"
#include <cstdio>
#include <psp2/power.h>
namespace direct {
struct PerformanceHud : HudFont {
    PerformanceWindow window;
    bool active=false,ready=false;
    int cpuMHz=0,gpuMHz=0;
    void text(float x,float y,const char* s,uint32_t color=0xe8f0ffff)const{
        HudFont::text(x,y,s,color,300);
    }
    void prepare(bool show,uint64_t now){
        if(show!=active){active=show;ready=false;window.reset(now);}
        HudFont::prepare(active);
    }
    void record(uint64_t now,uint64_t media,uint64_t logic,uint64_t present,PerformanceDrawStats stats){
        if(active&&window.add(now,media,logic,present,stats)){
            ready=true;cpuMHz=scePowerGetArmClockFrequency();gpuMHz=scePowerGetGpuClockFrequency();
        }
    }
    void draw()const{
        if(!active||!atlas)return;
        constexpr float x=16,y=16;
        rect(x-8,y-8,300,332,0x07111fff);
        text(x,y,"PERFORMANCE",0x7bdfb7ff);
        if(!ready){text(x,y+22,"WAITING FOR SAMPLE");return;}
        const auto& s=window.sample;char line[64];int row=1;
        const auto show=[&](const char* format,double a,double b=0){
            std::snprintf(line,sizeof(line),format,a,b);text(x,y+row++*18,line);
        };
        show("FPS %.1f",s.fps);
        show("FRAME %.2f MS",s.frameMs);
        show("MAX %.2f MS",s.maxMs);
        show("DRAW %.0f PEAK %.0f",s.draws,double(s.peakDraws));
        show("TRIS %.0f PASS %.1f",s.triangles,s.passes);
        show("MEDIA %.2f MS",s.mediaMs);
        show("LOGIC %.2f MS",s.logicMs);
        show("PRESENT %.2f MS",s.presentMs);
        show("CPU %.0f GPU %.0f MHZ",cpuMHz,gpuMHz);
        show("GPU WAIT %.2f MS",s.gpuWaitMs);
        show("DISP WAIT %.2f MS",s.queueWaitMs);
        show("UPLOAD %.1f /FRAME",s.uploads);
        show("UPLOAD %.2f MIB/FRAME",s.uploadMiB);
        show("UPLOAD %.2f MS",s.uploadMs);
        show("SHARED %.1f /FRAME",s.shared);
        text(x,y+row++*18,"TIMES INCLUDE WAITS",0xa5b6cfff);
        text(x,y+row*18,"DRAWS EXCLUDE HUD",0xa5b6cfff);
    }
};
}
