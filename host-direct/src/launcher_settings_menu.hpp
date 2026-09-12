#pragma once
#include "cpu3_setting.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct LauncherSettingsMenu {
    int row=0;
    // -1 back, 0 navigation, 1 toggle CPU3, 2 open clock submenu.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+2)%3;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%3;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=124&&y<334){row=(y-124)/70;choose=true;}}
        if(row==0&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 1;
        if(choose)return row==1?2:-1;
        return 0;
    }
    void prepare()const{
        for(auto s:{"启动器设置","CapUnlocker","超频设置","返回游戏选择","开","关",
            "读取失败","保存失败","○ 进入",
            "CapUnlocker：允许后台任务使用第四个 CPU 核心。",
            "切换后重启应用生效，需要已安装并启用插件。",
            "超频设置：分别调整全局和 OGV 动画期间的频率。",
            "↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回"})menu_prepare(s,24);
    }
    void draw(const Cpu3Setting& cpu3)const{
        rect(0,0,960,544,0x101b2bff);menu_text(160,70,24,"启动器设置");
        const char* labels[]={"CapUnlocker","超频设置","返回游戏选择"};
        for(int i=0;i<3;i++){float y=124+i*70;rect(160,y,640,58,i==row?0x286482ff:0x1c2838ff);
            menu_text(182,y+38,24,labels[i]);}
        const char* status=!cpu3.readable?"读取失败":cpu3.failed?"保存失败":cpu3.next?"开":"关";
        menu_text(656,162,24,status,cpu3.failed||!cpu3.readable?0xff8080ff:0xffffffff);
        menu_text(656,232,24,"○ 进入");
        menu_text(160,376,24,"CapUnlocker：允许后台任务使用第四个 CPU 核心。");
        menu_text(160,410,24,"切换后重启应用生效，需要已安装并启用插件。");
        menu_text(160,456,24,"超频设置：分别调整全局和 OGV 动画期间的频率。");
        menu_text(160,514,24,"↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回");
    }
};
}
