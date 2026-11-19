#pragma once
#include "gpu.hpp"
#include "game_platform.hpp"
#include "table_resolution.hpp"
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <cstdio>

namespace direct {
struct ResolutionNotice {
    static constexpr const char* chineseReadmeUrl="https://github.com/DeQxJ00/Art3m1sPSV#readme";
    static constexpr const char* englishReadmeUrl="https://github.com/DeQxJ00/Art3m1sPSV/blob/main/README.en.md";
    static constexpr unsigned linkLine=1;
    bool open=false,armed=false;
    VitaResolution size;
    TableResolution tableSize;std::string tableName;
    static bool needed(VitaResolution value){return !((value.width==960&&(value.height==540||value.height==544))||(value.width==720&&value.height==540));}
    void check(VitaResolution value){size=value;open=needed(value);armed=false;}
    void check_table(std::string name,std::string_view source){
        tableName=std::move(name);tableSize=table_resolution(source);
        if(tableSize.known())open|=needed({tableSize.width,tableSize.height});
    }
    // Require release of the launcher input before accepting either action.
    int input(uint32_t buttons,const SceTouchData& touch){
        if(!armed){if(!(buttons&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS))&&!touch.reportNum)armed=true;return 0;}
        if(buttons&SCE_CTRL_CROSS)return -1;
        if(buttons&SCE_CTRL_CIRCLE)return 1;
        if(touch.reportNum){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            const float linkX=48+menu_text_width(link_prefix(),kMenuNoteSize);
            const float linkRight=linkX+menu_text_width(link_label(),kMenuNoteSize);
            const float linkY=196+linkLine*(line_count()>6?29.0f:34.0f);
            if(y>=linkY-20&&y<linkY+8&&x>=linkX-8&&x<=linkRight+8){armed=false;return 2;}
            if(y>=420&&y<478){if(x>=500&&x<860)return 1;if(x>=100&&x<460)return -1;}}
        return 0;
    }
    const char* title()const{return uiLanguage==UiLanguage::English?"Resource compatibility":"资源适配提示";}
    const char* readme_url()const{return uiLanguage==UiLanguage::English?englishReadmeUrl:chineseReadmeUrl;}
    const char* link_prefix()const{return uiLanguage==UiLanguage::English?"Need help compressing resources? See the ":"不确定如何压制资源？请参阅本项目 ";}
    const char* link_label()const{return "Art3m1sPsv";}
    const char* link_suffix()const{return uiLanguage==UiLanguage::English?" README for one-click conversion.":" 的 README，通常可以一键完成转换。";}
    unsigned line_count()const{return 7;}
    const char* line(unsigned i)const{
        static const char* zh[]={
            "检测到 INI 或 TBL 中声明的分辨率高于 PSV 分辨率，资源尚未针对 PSV 进行适配。",
            "不确定如何压制资源？请参阅本项目 Art3m1sPsv 的 README，通常可以一键完成转换。",
            "未进行转换可能导致以下问题：",
            "图片过大可能导致加载卡顿或卡死；E-mote 人物可能无法加载；",
            "OP 动画或 OGV 视频特效可能无法播放；Shader 或特效合成场景帧率偏低。",
            "即使能够进入游戏，后续仍可能出现其他问题，影响游戏体验。",
            "是否仍要继续进入游戏？"
        };
        static const char* en[]={"The INI or TBL declares a resolution above PSV's; resources may not be adapted for PSV.",
            "Need help compressing resources? See the Art3m1sPsv README for one-click conversion.",
            "Without conversion, you may encounter these issues:",
            "Oversized images may stall or slow loading; E-mote characters may fail to load.",
            "OP animations or OGV video effects may fail; Shader or effects scenes may have low frame rates.",
            "Even if the game starts, other issues may affect the experience.","Continue into the game?"};
        return uiLanguage==UiLanguage::English?en[i]:zh[i];
    }
    const char* accept()const{return "○ 确认进入";}
    const char* cancel()const{return "× 返回";}
    void dimensions(char* out,size_t cap)const{std::snprintf(out,cap,"system.ini: %u x %u",size.width,size.height);}
    void table_dimensions(char* out,size_t cap)const{
        if(tableSize.known())std::snprintf(out,cap,"%s: %u x %u",tableName.c_str(),tableSize.width,tableSize.height);
        else std::snprintf(out,cap,"%s: %s",tableName.c_str(),uiLanguage==UiLanguage::English?"not statically confirmed":"无法静态确认");
    }
    void prepare()const{
        ui_prepare(title(),kMenuTitleSize);for(unsigned i=0;i<line_count();i++)ui_prepare(line(i),kMenuNoteSize);
        ui_prepare(link_prefix(),kMenuNoteSize);ui_prepare(link_label(),kMenuNoteSize);ui_prepare(link_suffix(),kMenuNoteSize);
        char text[80];dimensions(text,sizeof(text));ui_prepare(text,kMenuBodySize);
        if(!tableName.empty()){table_dimensions(text,sizeof(text));ui_prepare(text,kMenuNoteSize);}
        ui_prepare(accept(),kMenuBodySize);ui_prepare(cancel(),kMenuBodySize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(48,80,kMenuTitleSize,title());
        char text[80];dimensions(text,sizeof(text));ui_text(48,140,kMenuBodySize,text,0xe8cd87ff);
        if(!tableName.empty()){table_dimensions(text,sizeof(text));ui_text(48,174,kMenuNoteSize,text,0xe8cd87ff);}
        const unsigned count=line_count();const float lineHeight=29.0f;
        for(unsigned i=0;i<count;i++){
            const float y=196+i*lineHeight;
            if(i!=linkLine){ui_text(48,y,kMenuNoteSize,line(i));continue;}
            const float linkX=48+menu_text_width(link_prefix(),kMenuNoteSize);
            const float linkWidth=menu_text_width(link_label(),kMenuNoteSize);
            menu_text(48,y,kMenuNoteSize,link_prefix());
            menu_text(linkX,y,kMenuNoteSize,link_label(),0x66cfffff);
            menu_text(linkX+linkWidth,y,kMenuNoteSize,link_suffix());
            rect(linkX,y+2,linkWidth,1,0x66cfffff);
        }
        rect(100,420,360,58,0x1c2838ff);rect(500,420,360,58,0x286482ff);
        ui_text(160,457,kMenuBodySize,cancel());ui_text(560,457,kMenuBodySize,accept());
    }
};
}
