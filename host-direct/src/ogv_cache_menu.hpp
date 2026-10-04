#pragma once
#include "ogv_cache_settings.hpp"
#include "gpu.hpp"
#include <algorithm>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
namespace direct {
struct OgvCacheMenu{
    OgvCacheSettings value;int row=0;bool failed=false;
    static constexpr const char* labels[]={"OGV 预载与缓存","缓存组数上限","缓存容量上限","恢复默认","保存并返回"};
    static constexpr const char* notes[]={
        "彩色 OGV 与配套遮罩合计一组；无遮罩也算一组。",
        "两个上限同时生效，计入共享缓存总量。",
        "仅缓存压缩文件；播放仍需解码，不缓存整段画面。",
        "内存不足时回收闲置组；超过上限的组流式播放。",
        "仅当前游戏，下次启动生效。"};
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+4)%5;if(pressed&SCE_CTRL_DOWN)row=(row+1)%5;
        bool choose=pressed&SCE_CTRL_CIRCLE;int step=pressed&SCE_CTRL_LEFT?-1:pressed&SCE_CTRL_RIGHT?1:0;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=90&&x<870&&y>=105&&y<305){row=(y-105)/40;choose=true;if(row==1||row==2)step=x<650?-1:1;}}
        if(!choose&&!step)return 0;failed=false;
        if(row==0)value.enabled=!value.enabled;
        if(row==1&&value.enabled)value.groups=std::clamp(int(value.groups)+(step?step:1),1,16);
        if(row==2&&value.enabled)value.mib=std::clamp(int(value.mib)+4*(step?step:1),4,64);
        if(row==3&&choose)value={};if(row==4&&choose)return 1;return 0;
    }
    std::string text(int i)const{if(i==0)return value.enabled?"开启（默认）":"关闭";if(i==1)return std::to_string(value.groups)+" 组"+(value.groups==4?"（默认）":"");if(i==2)return std::to_string(value.mib)+" MiB"+(value.mib==16?"（默认）":"");return {};}
    void prepare()const{
        menu_prepare("OGV 预载与缓存",kMenuTitleSize);
        for(int i=0;i<5;++i){menu_prepare(labels[i],kMenuBodySize);menu_prepare(text(i).c_str(),kMenuBodySize);}
        for(auto note:notes)menu_prepare(note,kMenuNoteSize);
        menu_prepare("保存失败，请重试。",kMenuNoteSize);menu_prepare("↑↓ 选择   ←→ 调整   ○ 确认   × 取消",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);menu_text(90,60,kMenuTitleSize,"OGV 预载与缓存");
        for(int i=0;i<5;++i){const float y=105+i*40;rect(90,y,780,36,i==row?0x286482ff:0x1c2838ff);menu_text(112,y+28,kMenuBodySize,labels[i]);menu_text(590,y+28,kMenuBodySize,text(i).c_str());}
        for(int i=0;i<5;++i)menu_text(90,340+i*28,kMenuNoteSize,notes[i],0xb5c4d4ff);
        if(failed)menu_text(90,483,kMenuNoteSize,"保存失败，请重试。",0xff8080ff);
        menu_text(90,527,kMenuNoteSize,"↑↓ 选择   ←→ 调整   ○ 确认   × 取消");
    }
};
}
