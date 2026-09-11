#pragma once
#include "font_settings.hpp"
#include "fallback_menu.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <algorithm>

namespace direct {
struct FontSettingsMenu {
    FontSettings value;int row=0;bool failed=false;
    // 0 edits; 1 saves; -1 cancels. Changes take effect once, on Save.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+4)%5;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%5;
        int step=(pressed&SCE_CTRL_LEFT)?-5:(pressed&SCE_CTRL_RIGHT)?5:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=220&&x<740&&y>=110&&y<410){row=(y-110)/60;choose=true;
                if(row==1||row==2){step=x<480?-5:5;choose=false;}}}
        if(row==0&&(choose||step)){value.enabled=!value.enabled;failed=false;}
        if((row==1||row==2)&&value.enabled&&(step||choose)){
            auto& n=row==1?value.name:value.dialogue;n=unsigned(std::clamp(int(n)+(step?step:5),75,150));failed=false;}
        if(choose&&row==3){value={};failed=false;}
        if(choose&&row==4)return 1;
        return 0;
    }
    void draw() const {
        rect(0,0,960,544,0x101b2bff);fallback_menu_text(220,65,FallbackLabel::FontTitle);
        const FallbackLabel labels[]={FallbackLabel::FontEnable,FallbackLabel::FontName,FallbackLabel::FontDialogue,FallbackLabel::FontReset,FallbackLabel::FontSave};
        for(int i=0;i<5;i++){float y=110+i*60;rect(220,y,520,50,i==row?0x286482ff:0x1c2838ff);
            auto color=(i==1||i==2)&&!value.enabled?0x8895a5ff:0xffffffff;
            fallback_menu_text(240,y+34,labels[i],color);
            if(i==0)fallback_menu_text(640,y+34,value.enabled?FallbackLabel::FontOn:FallbackLabel::FontOff);
            if(i==1||i==2){char number[8];std::snprintf(number,sizeof(number),"%u",i==1?value.name:value.dialogue);float x=630;
                for(const char* c=number;*c;c++,x+=15)fallback_menu_text(x,y+34,FallbackLabel(unsigned(FallbackLabel::Digit0)+*c-'0'),color);
                fallback_menu_text(x,y+34,FallbackLabel::Percent,color);}}
        fallback_menu_text(220,450,failed?FallbackLabel::FontError:FallbackLabel::FontNote,failed?0xff8080ff:0xb5c4d4ff);
        fallback_menu_text(220,490,FallbackLabel::FontHelp);
    }
};
}
