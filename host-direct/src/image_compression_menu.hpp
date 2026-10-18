#pragma once
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
namespace direct {
struct ImageCompressionMenu {
    int row=0;
    static constexpr const char* labels[]={"大图材质压缩","透明区域压缩","返回游戏设置"};
    static constexpr const char* notes[]={"BC3 有损压缩，减少显存；可选自动或手动目录范围。","无损压缩透明区域，减少 CPU 内存；适合 image/fg。","各项按游戏保存，进入后可调整具体条件和目录。"};
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+2)%3;if(pressed&SCE_CTRL_DOWN)row=(row+1)%3;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=90&&x<870&&y>=125&&y<305){row=(y-125)/60;choose=true;}}
        return choose?(row==2?-1:row+1):0;
    }
    void prepare()const{
        ui_prepare("图片压缩",kMenuTitleSize);for(auto label:labels)ui_prepare(label,kMenuBodySize);
        for(auto note:notes)ui_prepare(note,kMenuNoteSize);
        ui_prepare("↑↓ 选择   ○ 进入   × 返回",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(90,65,kMenuTitleSize,"图片压缩");
        for(int i=0;i<3;++i){float y=125+i*60;rect(90,y,780,48,i==row?0x286482ff:0x1c2838ff);ui_setting_text(110,y+32,kMenuBodySize,labels[i]);}
        ui_text(90,375,kMenuNoteSize,notes[row],0xb5c4d4ff);
        ui_text(90,415,kMenuNoteSize,notes[2],0xb5c4d4ff);
        ui_text(90,519,kMenuNoteSize,"↑↓ 选择   ○ 进入   × 返回");
    }
};
}
