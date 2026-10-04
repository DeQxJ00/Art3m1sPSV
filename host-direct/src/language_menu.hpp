#pragma once
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
// Bilingual even before a language is chosen. Saving is handled by the launcher.
struct LanguageMenu {
    int row=0;
    bool failed=false;
    bool input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_UP|SCE_CTRL_DOWN))row=1-row;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap&&touch.reportNum){
            const int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=220&&x<740&&y>=206&&y<318){
                const int index=(y-206)/56;
                if((y-206)%56<48){row=index;choose=true;}
            }
        }
        return choose;
    }
    UiLanguage selected()const{return row==0?UiLanguage::Chinese:UiLanguage::English;}
    static constexpr const char* title="选择语言 / Choose language";
    static constexpr const char* note="之后可在设置中更改 / Change later in Settings";
    static constexpr const char* help="↑↓ 选择 / Select    ○ 确认 / OK    × 退出 / Exit";
    static constexpr const char* error="保存失败，请重试 / Save failed. Please retry.";
    void prepare()const{
        menu_prepare("Art3m1sPSV",kMenuTitleSize);
        menu_prepare(title,kMenuTitleSize);
        menu_prepare("简体中文",kMenuBodySize);menu_prepare("English",kMenuBodySize);
        for(const char* text:{note,help,error})menu_prepare(text,kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);
        menu_text(220,88,kMenuTitleSize,"Art3m1sPSV");
        menu_text(220,152,kMenuTitleSize,title);
        for(int i=0;i<2;++i){const int y=206+i*56;
            rect(220,y,520,48,i==row?0x286482ff:0x1c2838ff);
            menu_text(246,y+33,kMenuBodySize,i==0?"简体中文":"English");
        }
        menu_text(220,372,kMenuNoteSize,note,0xb5c4d4ff);
        if(failed)menu_text(220,415,kMenuNoteSize,error,0xff8080ff);
        menu_text(220,490,kMenuNoteSize,help);
    }
};
}
