#pragma once
#include "clock_settings.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct ClockSettingsMenu {
    ClockSettings value{};int row=0;bool failed=false,saved=false;
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+4)%5;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%5;
        int step=(pressed&SCE_CTRL_LEFT)?-1:(pressed&SCE_CTRL_RIGHT)?1:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=112&&y<352){row=(y-112)/48;choose=true;step=x<480?-1:1;}}
        if(row<4&&(choose||step)){
            int* values[]={&value.global,&value.es4Global,&value.ogv,&value.es4Ogv};
            int& v=*values[row];v=(row%2?next_es4_clock_choice:next_clock_choice)(v,step?step:1);failed=saved=false;}
        return row==4&&choose?1:0;
    }
    static const char* choice(int mhz){switch(mhz){case 111:return "111 MHz";case 166:return "166 MHz";
        case 222:return "222 MHz";case 444:return "444 MHz";default:return "关闭";}}
    void prepare(const ClockPolicy& cpu,const ClockPolicy& es4){
        for(auto text:{"超频设置","全局 CPU","全局 ES4（GPU）","OGV 动画 CPU","OGV 动画 ES4（GPU）","保存并应用","关闭","111 MHz","166 MHz","222 MHz","444 MHz",
            "全局关闭：保留系统频率；OGV 关闭：沿用全局。",
            "OGV 播放结束后恢复；MP4 不触发动画超频。",
            "CPU 与 ES4 分别设置；插件锁频可能阻止切换。",
            "↑↓ 选择   ←→ 调整   ○ 确认   × 返回",
            "保存失败，请重试。","已保存。","配置已保存，但请求频率未生效。"})menu_prepare(text,24);
        char state[96];std::snprintf(state,sizeof(state),"当前 CPU：%d MHz    ES4：%d MHz",cpu.actual,es4.actual);menu_prepare(state,24);
    }
    void draw(const ClockPolicy& cpu,const ClockPolicy& es4)const{
        rect(0,0,960,544,0x101b2bff);menu_text(160,60,24,"超频设置");
        const char* labels[]={"全局 CPU","全局 ES4（GPU）","OGV 动画 CPU","OGV 动画 ES4（GPU）","保存并应用"};
        const int values[]={value.global,value.es4Global,value.ogv,value.es4Ogv};
        for(int i=0;i<5;i++){float y=112+i*48;rect(160,y,640,40,i==row?0x286482ff:0x1c2838ff);
            menu_text(182,y+29,24,labels[i]);if(i<4)menu_text(642,y+29,24,choice(values[i]));}
        char state[96];std::snprintf(state,sizeof(state),"当前 CPU：%d MHz    ES4：%d MHz",cpu.actual,es4.actual);menu_text(160,96,24,state);
        menu_text(160,388,24,"全局关闭：保留系统频率；OGV 关闭：沿用全局。");
        menu_text(160,418,24,"OGV 播放结束后恢复；MP4 不触发动画超频。");
        menu_text(160,448,24,"CPU 与 ES4 分别设置；插件锁频可能阻止切换。");
        const bool denied=cpu.result<0||es4.result<0;
        if(failed)menu_text(160,482,24,"保存失败，请重试。",0xff8080ff);
        else if(saved||denied)menu_text(160,482,24,denied?"配置已保存，但请求频率未生效。":"已保存。",denied?0xff8080ff:0xb5c4d4ff);
        menu_text(160,514,24,"↑↓ 选择   ←→ 调整   ○ 确认   × 返回");
    }
};
}
