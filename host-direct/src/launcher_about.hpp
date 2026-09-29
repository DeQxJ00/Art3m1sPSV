#pragma once
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct LauncherAbout {
    bool close(uint32_t pressed,bool tap,const SceTouchData& touch)const{
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_CIRCLE|SCE_CTRL_SELECT))return true;
        if(!tap)return false;
        const int x=touch.report[0].x/2,y=touch.report[0].y/2;
        return x>=754&&x<924&&y>=474&&y<532;
    }
    void prepare()const{
        menu_prepare("关于",30);menu_prepare("开发者：DeQxJ00",24);
        menu_prepare("GitHub：github.com/DeQxJ00/Art3m1sPSV",22);
        menu_prepare("Credits",25);
        for(const char* line:{"Art3m1s / art3m1s-core","VitaSDK","vitaShaRK","FFmpeg",
            "Tremor","Lua / mlua","asb-decrypt","pfs-rs","image","ab_glyph",
            "stb","cJSON","VitaCompanion / Vita3K"})menu_prepare(line,20);
        menu_prepare("× / ○ 返回",22);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);
        menu_text(44,60,30,"关于");rect(44,80,872,2,0x354256ff);
        menu_text(64,135,24,"开发者：DeQxJ00");
        menu_text(64,175,22,"GitHub：github.com/DeQxJ00/Art3m1sPSV");
        menu_text(64,227,25,"Credits");rect(64,240,830,2,0x354256ff);
        const char* left[]={"Art3m1s / art3m1s-core","VitaSDK","vitaShaRK","FFmpeg",
            "Tremor","Lua / mlua","asb-decrypt"};
        const char* right[]={"pfs-rs","image","ab_glyph","stb","cJSON",
            "VitaCompanion / Vita3K"};
        for(unsigned i=0;i<7;i++)menu_text(70,277+i*33,20,left[i]);
        for(unsigned i=0;i<6;i++)menu_text(540,277+i*33,20,right[i]);
        rect(754,474,170,50,0x286482ff);
        menu_text(787,507,22,"× / ○ 返回");
    }
};
}
