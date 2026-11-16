#pragma once
#include "image_cache_settings.hpp"
#include "gpu.hpp"
#include <algorithm>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
namespace direct {
struct ImageCacheMenu {
    ImageCacheSettings value;int row=0;bool failed=false;
    static constexpr const char* labels[]={"Lua LOAD 缓存上限","预载内存上限","IDLE 内存上限","恢复默认","保存并返回"};
    static constexpr const char* notes[]={
        "Lua LOAD 限制缓存图片数量，不删除脚本绑定。",
        "预载包含已解码图片与压缩源；IDLE 保留闲置图片。",
        "三项受 192 MiB 共享总预算约束，不提前分配内存。",
        "超限图片按需加载；额度太小可能增加切图卡顿。",
        "仅当前游戏，下次启动生效。"};
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+4)%5;if(pressed&SCE_CTRL_DOWN)row=(row+1)%5;
        bool choose=pressed&SCE_CTRL_CIRCLE;int step=pressed&SCE_CTRL_LEFT?-1:pressed&SCE_CTRL_RIGHT?1:0;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=90&&x<870&&y>=105&&y<305){row=(y-105)/40;choose=true;if(row<3)step=x<650?-1:1;}}
        if(!choose&&!step)return 0;failed=false;
        if(row==0)value.luaCount=std::clamp(int(value.luaCount)+16*(step?step:1),16,4096);
        if(row==1)value.readyMib=std::clamp(int(value.readyMib)+8*(step?step:1),8,192);
        if(row==2)value.idleMib=std::clamp(int(value.idleMib)+8*(step?step:1),8,192);
        if(row==3&&choose)value={};if(row==4&&choose)return 1;return 0;
    }
    std::string text(int i)const{
        if(i>2)return {};
        const unsigned n=i==0?value.luaCount:i==1?value.readyMib:value.idleMib;
        return std::to_string(n)+(i==0?"":" MiB")+((i==0?n==128:i==1?n==64:n==96)?ui_translate("（默认）"):"");
    }
    void prepare()const{
        ui_prepare("图片缓存",kMenuTitleSize);
        for(int i=0;i<5;++i){ui_prepare(labels[i],kMenuBodySize);ui_prepare(text(i).c_str(),kMenuBodySize);}
        for(auto note:notes)ui_prepare(note,kMenuNoteSize);
        ui_prepare("保存失败，请重试。",kMenuNoteSize);ui_prepare("↑↓ 选择   ←→ 调整   ○ 确认   × 取消",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(90,60,kMenuTitleSize,"图片缓存");
        for(int i=0;i<5;++i){const float y=105+i*40;rect(90,y,780,36,i==row?0x286482ff:0x1c2838ff);ui_setting_text(112,y+28,kMenuBodySize,labels[i]);ui_setting_text(550,y+28,kMenuBodySize,text(i).c_str());}
        for(int i=0;i<5;++i)ui_text(90,340+i*26,kMenuNoteSize,notes[i],0xb5c4d4ff);
        if(failed)ui_text(90,483,kMenuNoteSize,"保存失败，请重试。",0xff8080ff);
        ui_text(90,527,kMenuNoteSize,"↑↓ 选择   ←→ 调整   ○ 确认   × 取消");
    }
};
}
