#pragma once
#include "font_settings.hpp"
#include "fallback_menu.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <algorithm>

namespace direct {
struct FontSettingsMenu {
    FontSettings value;int row=0;bool failed=false,applyFailed=false;
    // 0 edits; 1 saves; -1 cancels. Changes take effect once, on Save.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+10)%11;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%11;
        int step=(pressed&SCE_CTRL_LEFT)?-5:(pressed&SCE_CTRL_RIGHT)?5:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=220&&x<740&&y>=85&&y<459){row=(y-85)/34;choose=true;
                if(row==1||row==2||(row>=4&&row<=7)){step=x<480?-5:5;choose=false;}}}
        if(row==0&&(choose||step)){value.enabled=!value.enabled;failed=false;}
        if((row==1||row==2)&&value.enabled&&(step||choose)){
            auto& n=row==1?value.name:value.dialogue;n=unsigned(std::clamp(int(n)+(step?step:5),75,150));failed=false;}
        if(row==3&&(choose||step)){value.positionEnabled=!value.positionEnabled;failed=false;}
        if(row>=4&&row<=7&&value.positionEnabled&&(step||choose)){
            int* offsets[]={&value.chineseX,&value.chineseY,&value.japaneseX,&value.japaneseY};
            auto& n=*offsets[row-4];n=std::clamp(n+(step?step:5),-500,500);failed=false;}
        if(row==8&&(choose||step)){value.hideJapanese=!value.hideJapanese;failed=false;}
        if(choose&&row==9){value={};failed=false;}
        if(choose&&row==10)return 1;
        return 0;
    }
    void draw() const {
        rect(0,0,960,544,0x101b2bff);fallback_menu_text(220,65,FallbackLabel::FontTitle);
        const FallbackLabel labels[]={FallbackLabel::FontEnable,FallbackLabel::FontName,FallbackLabel::FontDialogue,
            FallbackLabel::PositionEnable,FallbackLabel::ChineseX,FallbackLabel::ChineseY,FallbackLabel::JapaneseX,
            FallbackLabel::JapaneseY,FallbackLabel::HideJapanese,FallbackLabel::FontReset,FallbackLabel::FontSave};
        const int offsets[]={value.chineseX,value.chineseY,value.japaneseX,value.japaneseY};
        for(int i=0;i<11;i++){float y=85+i*34;rect(220,y,520,30,i==row?0x286482ff:0x1c2838ff);
            auto color=(((i==1||i==2)&&!value.enabled)||(i>=4&&i<=7&&!value.positionEnabled))?0x8895a5ff:0xffffffff;
            fallback_menu_text(240,y+24,labels[i],color);
            if(i==0||i==3||i==8){bool on=i==0?value.enabled:i==3?value.positionEnabled:value.hideJapanese;
                fallback_menu_text(640,y+24,on?FallbackLabel::FontOn:FallbackLabel::FontOff);}
            if(i==1||i==2||(i>=4&&i<=7)){char number[8];std::snprintf(number,sizeof(number),"%d",i==1?int(value.name):i==2?int(value.dialogue):offsets[i-4]);float x=620;
                for(const char* c=number;*c;c++,x+=15)fallback_menu_text(x,y+24,*c=='-'?FallbackLabel::Minus:FallbackLabel(unsigned(FallbackLabel::Digit0)+*c-'0'),color);
                if(i==1||i==2)fallback_menu_text(x,y+24,FallbackLabel::Percent,color);}}
        fallback_menu_text(220,486,failed?(applyFailed?FallbackLabel::FontApplyError:FallbackLabel::FontError):FallbackLabel::PositionNote,failed?0xff8080ff:0xb5c4d4ff);
        fallback_menu_text(220,520,FallbackLabel::FontHelp);
    }
};
}
