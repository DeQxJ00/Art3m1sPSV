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
        if(pressed&SCE_CTRL_UP)row=(row+8)%9;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%9;
        int step=(pressed&SCE_CTRL_LEFT)?-1:(pressed&SCE_CTRL_RIGHT)?1:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=104&&y<392){row=(y-104)/32;choose=true;step=x<480?-1:1;}}
        if(row<8&&(choose||step)){
            int* values[]={&value.global,&value.es4Global,&value.ogv,&value.es4Ogv,&value.effectPanCpu,&value.effectPanEs4,&value.emoteCpu,&value.emoteEs4};
            int& v=*values[row];v=(row%2?next_es4_clock_choice:next_clock_choice)(v,step?step:1);failed=saved=false;}
        return row==8&&choose?1:0;
    }
    static const char* choice(int mhz){switch(mhz){case 111:return "111 MHz";case 166:return "166 MHz";
        case 222:return "222 MHz";case 444:return "444 MHz";default:return "关闭";}}
    void prepare(const ClockPolicy& cpu,const ClockPolicy& es4){
        ui_prepare("超频设置",kMenuTitleSize);
        for(auto text:{"全局 CPU","全局 ES4（GPU）","OGV 动画 CPU",
            "OGV 动画 ES4（GPU）","特效平移 CPU","特效平移 ES4（GPU）",
            "E-mote 场景 CPU","E-mote 场景 ES4（GPU）","保存并应用",
            "关闭","111 MHz","166 MHz",
            "222 MHz","444 MHz"})ui_prepare(text,kMenuBodySize);
        for(auto text:{"全局关闭：保留系统频率；OGV 关闭：沿用全局。","OGV 播放结束后恢复；MP4 不触发动画超频。","平移两项独立设置；关闭沿用，结束后恢复。",
            "E-mote 默认 444 / 222；人物隐藏或移除后恢复。","场景同时生效取较高值；插件锁频可能阻止切换。","↑↓ 选择   ←→ 调整   ○ 确认   × 返回",
            "保存失败，请重试。","已保存。","配置已保存，但请求频率未生效。"})ui_prepare(text,kMenuNoteSize);

        char state[96];std::snprintf(state,sizeof(state),direct::ui_translate("当前 CPU：%d MHz    ES4：%d MHz"),cpu.actual,es4.actual);ui_prepare(state,kMenuNoteSize);
    }
    void draw(const ClockPolicy& cpu,const ClockPolicy& es4)const{
        rect(0,0,960,544,0x101b2bff);ui_text(160,52,kMenuTitleSize,"超频设置");
        const char* labels[]={"全局 CPU","全局 ES4（GPU）","OGV 动画 CPU","OGV 动画 ES4（GPU）","特效平移 CPU","特效平移 ES4（GPU）","E-mote 场景 CPU","E-mote 场景 ES4（GPU）","保存并应用"};
        const int values[]={value.global,value.es4Global,value.ogv,value.es4Ogv,value.effectPanCpu,value.effectPanEs4,value.emoteCpu,value.emoteEs4};
        for(int i=0;i<9;i++){float y=104+i*32;rect(160,y,640,30,i==row?0x286482ff:0x1c2838ff);
            ui_setting_text(182,y+26,kMenuBodySize,labels[i]);if(i<8)ui_setting_text(642,y+26,kMenuBodySize,choice(values[i]));}
        char state[96];std::snprintf(state,sizeof(state),direct::ui_translate("当前 CPU：%d MHz    ES4：%d MHz"),cpu.actual,es4.actual);ui_text(160,88,kMenuNoteSize,state);
        ui_text(160,420,kMenuNoteSize,"全局关闭：保留系统频率；OGV 关闭：沿用全局。");
        ui_text(160,448,kMenuNoteSize,(row==6||row==7)?"E-mote 默认 444 / 222；人物隐藏或移除后恢复。":(row==4||row==5)?"平移两项独立设置；关闭沿用，结束后恢复。":"OGV 播放结束后恢复；MP4 不触发动画超频。");
        ui_text(160,476,kMenuNoteSize,"场景同时生效取较高值；插件锁频可能阻止切换。");
        const bool denied=cpu.result<0||es4.result<0;
        if(failed)ui_text(160,504,kMenuNoteSize,"保存失败，请重试。",0xff8080ff);
        else if(saved||denied)ui_text(160,504,kMenuNoteSize,denied?"配置已保存，但请求频率未生效。":"已保存。",denied?0xff8080ff:0xb5c4d4ff);
        ui_text(160,532,kMenuNoteSize,"↑↓ 选择   ←→ 调整   ○ 确认   × 返回");
    }
};
}
