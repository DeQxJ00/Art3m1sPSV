#pragma once
#include "gpu.hpp"
#include "game_platform.hpp"
#include "table_resolution.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <cstdio>

namespace direct {
struct ResolutionNotice {
    bool open=false,armed=false;
    VitaResolution size;
    TableResolution tableSize;std::string tableName;
    static bool needed(VitaResolution value){return value.width!=960||(value.height!=540&&value.height!=544);}
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
            if(y>=420&&y<478){if(x>=500&&x<860)return 1;if(x>=100&&x<460)return -1;}}
        return 0;
    }
    const char* title()const{return uiLanguage==UiLanguage::English?"Resource compatibility":"资源适配提示";}
    const char* line(unsigned i)const{
        static const char* zh[]={"INI 或对应 TBL 声明了非 PSV 常用分辨率。","据此推测，本资源可能尚未进行适配压缩。","即使能够进入游戏，后续仍可能出现问题，影响游戏体验。","是否仍要继续进入游戏？"};
        static const char* en[]={"The INI or matching TBL declares a non-standard PSV resolution.","This suggests the resources may not have been adapted or compressed.","Even if the game starts, later issues may affect the experience.","Continue into the game?"};
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
        ui_prepare(title(),kMenuTitleSize);for(unsigned i=0;i<4;i++)ui_prepare(line(i),kMenuNoteSize);
        char text[80];dimensions(text,sizeof(text));ui_prepare(text,kMenuBodySize);
        if(!tableName.empty()){table_dimensions(text,sizeof(text));ui_prepare(text,kMenuNoteSize);}
        ui_prepare(accept(),kMenuBodySize);ui_prepare(cancel(),kMenuBodySize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(48,80,kMenuTitleSize,title());
        char text[80];dimensions(text,sizeof(text));ui_text(48,140,kMenuBodySize,text,0xe8cd87ff);
        if(!tableName.empty()){table_dimensions(text,sizeof(text));ui_text(48,174,kMenuNoteSize,text,0xe8cd87ff);}
        for(unsigned i=0;i<4;i++)ui_text(48,208+i*44,kMenuNoteSize,line(i));
        rect(100,420,360,58,0x1c2838ff);rect(500,420,360,58,0x286482ff);
        ui_text(160,457,kMenuBodySize,cancel());ui_text(560,457,kMenuBodySize,accept());
    }
};
}
