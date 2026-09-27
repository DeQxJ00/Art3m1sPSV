#pragma once
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct GameSettingsMenu {
    int row=0;bool windows=false,failed=false,toolbarHidden=false;
    // -1 back, 1 font submenu, 2 platform toggle, 3 toolbar toggle.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+3)%4;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%4;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=140&&y<380){row=(y-140)/60;choose=true;}}
        if(row==1&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 2;
        if(row==2&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 3;
        if(choose)return row==0?1:-1;
        return 0;
    }
    void prepare(const char* id)const {
        for(auto text:{"当前游戏设置","字号设置","运行平台","Vita（默认）","Windows","返回游戏选择","隐藏顶部工具栏","开启","关闭（默认）",
            "○ 进入","仅对当前游戏生效，切换后下次启动使用。",
            "隐藏游戏的触控工具栏，保留正文和底部按钮。",
            "保存失败，仍使用原设置。","↑↓ 选择   ○ 确认   ←→ 调整   × 返回"})menu_prepare(text,24);
        menu_prepare(id,22);
    }
    void draw(const char* id)const {
        rect(0,0,960,544,0x101b2bff);menu_text(160,60,24,"当前游戏设置");menu_text(160,100,22,id);
        const char* labels[]={"字号设置","运行平台","隐藏顶部工具栏","返回游戏选择"};
        for(int i=0;i<4;i++){float y=140+i*60;rect(160,y,640,50,i==row?0x286482ff:0x1c2838ff);menu_text(182,y+34,24,labels[i]);}
        menu_text(650,174,24,"○ 进入");menu_text(600,234,24,windows?"Windows":"Vita（默认）");
        menu_text(600,294,24,toolbarHidden?"开启":"关闭（默认）");
        menu_text(160,420,24,failed?"保存失败，仍使用原设置。":"仅对当前游戏生效，切换后下次启动使用。",failed?0xff8080ff:0xb5c4d4ff);
        menu_text(160,455,24,"隐藏游戏的触控工具栏，保留正文和底部按钮。",0xb5c4d4ff);
        menu_text(160,510,24,"↑↓ 选择   ○ 确认   ←→ 调整   × 返回");
    }
};
}
