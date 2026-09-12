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
        if(pressed&SCE_CTRL_UP)row=(row+2)%3;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%3;
        int step=(pressed&SCE_CTRL_LEFT)?-1:(pressed&SCE_CTRL_RIGHT)?1:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=124&&y<334){row=(y-124)/70;choose=true;step=x<480?-1:1;}}
        if(row<2&&(choose||step)){int& v=row==0?value.global:value.ogv;
            v=next_clock_choice(v,step?step:1);failed=saved=false;}
        return row==2&&choose?1:0;
    }
    static const char* choice(int mhz){return mhz==444?"444 MHz":mhz==500?"500 MHz":"关闭";}
    void prepare(const CpuClockPolicy& policy){
        for(auto text:{"CPU 超频设置","全局超频","OGV 动画超频","保存并应用","关闭","444 MHz","500 MHz",
            "全局关闭：保留系统频率；OGV 关闭：沿用全局。",
            "OGV 播放结束后恢复；MP4 不触发动画超频。",
            "500 MHz 需超频插件支持；插件锁频可能阻止切换。",
            "↑↓ 选择   ←→ 调整   ○ 确认   × 返回",
            "保存失败，请重试。","已保存。","配置已保存，但请求频率未生效。"})menu_prepare(text,24);
        char state[80];std::snprintf(state,sizeof(state),"当前 CPU：%d MHz",policy.actual);menu_prepare(state,24);
    }
    void draw(const CpuClockPolicy& policy)const{
        rect(0,0,960,544,0x101b2bff);menu_text(160,70,24,"CPU 超频设置");
        const char* labels[]={"全局超频","OGV 动画超频","保存并应用"};
        for(int i=0;i<3;i++){float y=124+i*70;rect(160,y,640,58,i==row?0x286482ff:0x1c2838ff);
            menu_text(182,y+38,24,labels[i]);if(i<2)menu_text(642,y+38,24,choice(i==0?value.global:value.ogv));}
        char state[80];std::snprintf(state,sizeof(state),"当前 CPU：%d MHz",policy.actual);menu_text(160,106,24,state);
        menu_text(160,366,24,"全局关闭：保留系统频率；OGV 关闭：沿用全局。");
        menu_text(160,398,24,"OGV 播放结束后恢复；MP4 不触发动画超频。");
        menu_text(160,430,24,"500 MHz 需超频插件支持；插件锁频可能阻止切换。");
        if(failed)menu_text(160,468,24,"保存失败，请重试。",0xff8080ff);
        else if(saved)menu_text(160,468,24,policy.result<0?"配置已保存，但请求频率未生效。":"已保存。",policy.result<0?0xff8080ff:0xb5c4d4ff);
        menu_text(160,514,24,"↑↓ 选择   ←→ 调整   ○ 确认   × 返回");
    }
};
}
