#pragma once
#include "psb_cache_settings.hpp"
#include "gpu.hpp"
#include <algorithm>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
namespace direct {
struct PsbCacheMenu {
    PsbCacheSettings value;int row=0;bool failed=false;
    static constexpr const char* labels[]={"PSB 缓存个数","PSB 缓存容量","恢复默认","保存并返回"};
    static constexpr const char* notes[]={
        "个数与容量同时限制，内存不足时实际缓存可能更少。",
        "容量包含 PSB 数据与解析模型，不包含 GPU 纹理。",
        "计入共享缓存总量；超出容量的模型仍可正常播放。",
        "默认 4 个、32 MiB；预读计划仍最多 2 个。",
        "仅当前游戏，下次启动生效。"};
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+3)%4;if(pressed&SCE_CTRL_DOWN)row=(row+1)%4;
        bool choose=pressed&SCE_CTRL_CIRCLE;int step=pressed&SCE_CTRL_LEFT?-1:pressed&SCE_CTRL_RIGHT?1:0;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=90&&x<870&&y>=105&&y<265){row=(y-105)/40;choose=true;if(row<2)step=x<650?-1:1;}}
        if(!choose&&!step)return 0;failed=false;
        if(row==0)value.groups=std::clamp(int(value.groups)+(step?step:1),1,16);
        if(row==1)value.mib=std::clamp(int(value.mib)+8*(step?step:1),8,128);
        if(row==2&&choose)value={};if(row==3&&choose)return 1;return 0;
    }
    std::string text(int i)const{if(i==0)return std::to_string(value.groups)+(value.groups==4?ui_translate("（默认）"):"");if(i==1)return std::to_string(value.mib)+" MiB"+(value.mib==32?ui_translate("（默认）"):"");return {};}
    void prepare()const{
        ui_prepare("PSB 模型缓存",kMenuTitleSize);
        for(int i=0;i<4;++i){ui_prepare(labels[i],kMenuBodySize);ui_prepare(text(i).c_str(),kMenuBodySize);}
        for(auto note:notes)ui_prepare(note,kMenuNoteSize);
        ui_prepare("保存失败，请重试。",kMenuNoteSize);ui_prepare("↑↓ 选择   ←→ 调整   ○ 确认   × 取消",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(90,60,kMenuTitleSize,"PSB 模型缓存");
        for(int i=0;i<4;++i){const float y=105+i*40;rect(90,y,780,36,i==row?0x286482ff:0x1c2838ff);ui_setting_text(112,y+28,kMenuBodySize,labels[i]);ui_setting_text(590,y+28,kMenuBodySize,text(i).c_str());}
        for(int i=0;i<5;++i)ui_text(90,310+i*28,kMenuNoteSize,notes[i],0xb5c4d4ff);
        if(failed)ui_text(90,483,kMenuNoteSize,"保存失败，请重试。",0xff8080ff);
        ui_text(90,527,kMenuNoteSize,"↑↓ 选择   ←→ 调整   ○ 确认   × 取消");
    }
};
}
