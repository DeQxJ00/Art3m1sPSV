#pragma once
#include "font_settings.hpp"
#include "fallback_menu.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <algorithm>

namespace direct {
struct FontSettingsMenu {
    enum Item { Enable, Name, Dialogue, Subtitle, Position, BodyX, BodyY, SubX, SubY, HideSub, Choice, History, Reset, Save };
    inline static constexpr Item pages[][8]={{Enable,Name,Dialogue,Subtitle,Choice,History,Reset,Save},
        {Position,BodyX,BodyY,SubX,SubY,HideSub,Reset,Save}};
    static constexpr int pageCount=sizeof(pages)/sizeof(pages[0]), rowCount=8;
    FontSettings value;int row=0,page=0;bool failed=false,applyFailed=false;
    // 0 edits; 1 saves; -1 cancels. Changes take effect once, on Save.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_LTRIGGER){page=(page+pageCount-1)%pageCount;return 0;}
        if(pressed&SCE_CTRL_RTRIGGER){page=(page+1)%pageCount;return 0;}
        if(pressed&SCE_CTRL_UP)row=(row+rowCount-1)%rowCount;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%rowCount;
        int step=(pressed&SCE_CTRL_LEFT)?-5:(pressed&SCE_CTRL_RIGHT)?5:0;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(y>=62&&y<106&&x>=710&&x<850){page=(page+(x<780?pageCount-1:1))%pageCount;return 0;}
            if(x>=120&&x<840&&y>=120&&y<440){row=(y-120)/40;choose=true;
                auto item=pages[page][row];
                if(item==Name||item==Dialogue||item==Subtitle||item==Choice||item==History||(item>=BodyX&&item<=SubY)){step=x<650?-5:5;choose=false;}}
        }
        const int item=pages[page][row];
        if(item==0&&(choose||step)){value.enabled=!value.enabled;failed=false;}
        if(((item>=1&&item<=3)||(item==10||item==11))&&value.enabled&&(step||choose)){
            auto& n=item==1?value.name:item==2?value.dialogue:item==3?value.japanese:item==10?value.choice:value.backlog;n=unsigned(std::clamp(int(n)+(step?step:5),75,150));failed=false;}
        if(item==4&&(choose||step)){value.positionEnabled=!value.positionEnabled;failed=false;}
        if(item>=5&&item<=8&&value.positionEnabled&&(step||choose)){
            int* offsets[]={&value.chineseX,&value.chineseY,&value.japaneseX,&value.japaneseY};
            auto& n=*offsets[item-5];n=std::clamp(n+(step?step:5),-500,500);failed=false;}
        if(item==9&&(choose||step)){value.hideJapanese=!value.hideJapanese;failed=false;}
        if(choose&&item==12){value={};failed=false;}
        if(choose&&item==13)return 1;
        return 0;
    }
    void draw() const {
        rect(0,0,960,544,0x101b2bff);
        fallback_menu_text(120,52,FallbackLabel::FontTitle);
        fallback_menu_text(120,94,page==0?FallbackLabel::FontSizesPage:FallbackLabel::FontPositionPage,0xb5c4d4ff);
        fallback_menu_page_button(728,68,false);fallback_menu_page_button(798,68,true);
        const FallbackLabel labels[]={FallbackLabel::FontEnable,FallbackLabel::FontName,FallbackLabel::FontDialogue,FallbackLabel::FontJapanese,
            FallbackLabel::PositionEnable,FallbackLabel::ChineseX,FallbackLabel::ChineseY,FallbackLabel::JapaneseX,
            FallbackLabel::JapaneseY,FallbackLabel::HideJapanese,FallbackLabel::FontChoice,FallbackLabel::FontBacklog,FallbackLabel::FontReset,FallbackLabel::FontSave};
        const int offsets[]={value.chineseX,value.chineseY,value.japaneseX,value.japaneseY};
        for(int r=0;r<rowCount;r++){
            const int i=pages[page][r];float y=120+r*40;rect(120,y,720,36,r==row?0x286482ff:0x1c2838ff);
            bool size=(i>=Name&&i<=Subtitle)||i==Choice||i==History;
            bool offset=i>=BodyX&&i<=SubY;
            auto color=((size&&!value.enabled)||(offset&&!value.positionEnabled))?0x8895a5ff:0xffffffff;
            fallback_menu_text(140,settings_text_baseline(y+28),labels[i],color);
            if(i==Enable||i==Position||i==HideSub){bool on=i==Enable?value.enabled:i==Position?value.positionEnabled:value.hideJapanese;
                fallback_menu_text(740,settings_text_baseline(y+28),on?FallbackLabel::FontOn:FallbackLabel::FontOff);}
            if(size||offset){char number[8];std::snprintf(number,sizeof(number),"%d",i==Name?int(value.name):i==Dialogue?int(value.dialogue):i==Subtitle?int(value.japanese):i==Choice?int(value.choice):i==History?int(value.backlog):offsets[i-BodyX]);float x=720;
                for(const char* c=number;*c;c++,x+=15)fallback_menu_text(x,settings_text_baseline(y+28),*c=='-'?FallbackLabel::Minus:FallbackLabel(unsigned(FallbackLabel::Digit0)+*c-'0'),color);
                if(size)fallback_menu_text(x,settings_text_baseline(y+28),FallbackLabel::Percent,color);
            }
        }
        fallback_menu_text(120,478,failed?(applyFailed?FallbackLabel::FontApplyError:FallbackLabel::FontError):page==0?FallbackLabel::FontNote:FallbackLabel::PositionNote,failed?0xff8080ff:0xb5c4d4ff);
        fallback_menu_text(120,520,FallbackLabel::FontHelp);
    }
};
}
