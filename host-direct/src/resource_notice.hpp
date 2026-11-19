#pragma once
#include "gpu.hpp"
#include "game_library.hpp"
#include <psp2/touch.h>
#include <cstdio>

namespace direct {
struct ResourceNotice {
    static constexpr const char* path="ux0:data/art3m1s-gxm/resource-notice-seen.txt";
    static constexpr const char* chineseReadmeUrl="https://github.com/DeQxJ00/Art3m1sPSV";
    static constexpr const char* englishReadmeUrl="https://github.com/DeQxJ00/Art3m1sPSV/blob/main/README.en.md";
    static constexpr const char* toolUrl="https://github.com/DeQxJ00/Art3m1sPsvPortTool";
    bool failed=false;
    static bool needed(const std::vector<art3m1s::GameEntry>& games){
        for(const auto& game:games)if(!game.bundled&&game.ready())return false;
        FILE* file=std::fopen(path,"rb");
        if(!file)return true;
        const bool seen=std::fgetc(file)=='1';std::fclose(file);
        return !seen;
    }
    bool acknowledge(){
        FILE* file=std::fopen(path,"wb");
        if(!file){failed=true;return false;}
        const bool written=std::fputs("1\n",file)>=0;
        const bool closed=std::fclose(file)==0;
        failed=!(written&&closed);
        return !failed;
    }
    bool readme_tapped(bool tap,const SceTouchData& touch)const{
        if(!tap||!touch.reportNum)return false;
        const int x=touch.report[0].x/2,y=touch.report[0].y/2;
        const float linkX=48+menu_text_width(link_prefix_text(),kMenuNoteSize);
        const float linkRight=linkX+menu_text_width(link_label,kMenuNoteSize);
        return y>=352&&y<382&&x>=linkX-8&&x<=linkRight+8;
    }
    const char* tool_prefix()const{return ui_translate("开始前，请使用配套转换工具 ");}
    static constexpr const char* tool_label="Art3m1sPsvPortTool";
    const char* tool_suffix()const{return ui_translate(" 处理游戏资源。");}
    bool tool_tapped(bool tap,const SceTouchData& touch)const{
        if(!tap||!touch.reportNum)return false;
        const int x=touch.report[0].x/2,y=touch.report[0].y/2;
        const float linkX=48+menu_text_width(tool_prefix(),kMenuNoteSize);
        const float linkRight=linkX+menu_text_width(tool_label,kMenuNoteSize);
        return y>=122&&y<154&&x>=linkX-8&&x<=linkRight+8;
    }
    static constexpr const char* title="游戏资源准备";
    static constexpr const char* lines[]={
        "开始前，请使用配套转换工具 Art3m1sPsvPortTool 处理游戏资源。",
        "工具会自动缩放并转换图片、视频、偏移等数据。",
        "处理完成后，请将资源放入：",
        "ux0:/data/art3m1s-gxm/games/",
        "每个游戏单独放在一个子文件夹中。",
        "详细步骤请查看 Art3m1sPsv README。",
    };
    static constexpr const char* link_prefix="详细步骤请查看 ";
    static constexpr const char* link_label="Art3m1sPsv README";
    const char* link_prefix_text()const{return ui_translate(link_prefix);}
    const char* link_suffix_text()const{return uiLanguage==UiLanguage::English?".":"。";}
    const char* readme_url()const{return uiLanguage==UiLanguage::English?englishReadmeUrl:chineseReadmeUrl;}
    void prepare()const{
        ui_prepare(title,kMenuTitleSize);
        for(const auto* line:lines)ui_prepare(line,kMenuNoteSize);
        ui_prepare("开始前，请使用配套转换工具 ",kMenuNoteSize);ui_prepare(tool_label,kMenuNoteSize);ui_prepare(" 处理游戏资源。",kMenuNoteSize);
        ui_prepare(link_prefix,kMenuNoteSize);ui_prepare(link_label,kMenuNoteSize);ui_prepare("。",kMenuNoteSize);ui_prepare(".",kMenuNoteSize);
        ui_prepare("○ 确认",kMenuBodySize);
        if(failed)ui_prepare("保存失败，请重试",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);
        ui_text(48,72,kMenuTitleSize,title);
        const float toolY=142;
        const float toolX=48+menu_text_width(tool_prefix(),kMenuNoteSize);
        const float toolWidth=menu_text_width(tool_label,kMenuNoteSize);
        menu_text(48,toolY,kMenuNoteSize,tool_prefix());
        menu_text(toolX,toolY,kMenuNoteSize,tool_label,0x66cfffff);
        menu_text(toolX+toolWidth,toolY,kMenuNoteSize,tool_suffix());
        rect(toolX,toolY+2,toolWidth,1,0x66cfffff);
        for(unsigned i=1;i<5;++i)ui_text(48,142+46*i,kMenuNoteSize,lines[i]);
        const float y=142+46*5;
        const char* prefix=link_prefix_text();const char* suffix=link_suffix_text();
        const float linkX=48+menu_text_width(prefix,kMenuNoteSize);
        const float linkWidth=menu_text_width(link_label,kMenuNoteSize);
        menu_text(48,y,kMenuNoteSize,prefix);
        menu_text(linkX,y,kMenuNoteSize,link_label,0x66cfffff);
        menu_text(linkX+linkWidth,y,kMenuNoteSize,suffix);
        rect(linkX,y+2,linkWidth,1,0x66cfffff);
        if(failed)ui_text(48,420,kMenuNoteSize,"保存失败，请重试",0xff8080ff);
        rect(320,452,320,52,0x286482ff);
        ui_text(416,487,kMenuBodySize,"○ 确认");
    }
};
}
