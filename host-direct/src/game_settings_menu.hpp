#pragma once
#include "gpu.hpp"
#include "emote_mesh_settings.hpp"
#include "background_alpha_settings.hpp"
#include "game_platform.hpp"
#include "../../host/files.h"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct GameSettingsMenu {
    // Keep menu rows, touch bounds and the platform summary on one layout.
    static constexpr int rowsTop=125,rowPitch=32,rowHeight=29;
    static constexpr int overviewTop=350,overviewBaseline=373,overviewPitch=28;
    int row=0;unsigned platform=0;bool failed=false;
    unsigned emoteMesh=kDefaultEmoteMesh;
    bool ignoreBackgroundAlpha=false;
    bool resourcesPending=true;
    int resources[6]={};
    static constexpr const char* overviewLabels[]={"Vita","Windows","Switch","Android","iOS","PS4"};
    static constexpr const char* backgroundAlphaHelp[]={
        "部分 BG 自带透明度；忽略透明度可能加快场景合成。",
        "开启后有些画面可能显示错误，通常请保持关闭。",
        "仅在明确能加速且画面正常时开启。"};
    static const char* resource_label(int status){
        switch(status){
        case HOST_PLATFORM_MATCHED:return "资源匹配";
        case HOST_PLATFORM_FALLBACK:return "自动补表";
        case HOST_PLATFORM_NO_TABLE:return "缺少表";
        case HOST_PLATFORM_NO_IMAGES:return "缺少资源";
        default:return "待确认";
        }
    }
    static uint32_t resource_color(int status){
        return status==HOST_PLATFORM_MATCHED?0x91d9b0ff:status==HOST_PLATFORM_FALLBACK?0xe8cd87ff:
            status==HOST_PLATFORM_NO_TABLE||status==HOST_PLATFORM_NO_IMAGES?0xff9292ff:0xb5c4d4ff;
    }
    const char* resource_detail()const{
        if(resourcesPending)return "正在检查平台表和图片资源。";
        switch(resources[platform<kGamePlatformCount?platform:0]){
        case HOST_PLATFORM_MATCHED:return "已找到平台表及对应图片；不保证启动脚本兼容。";
        case HOST_PLATFORM_FALLBACK:return "缺少原生表，将使用资源匹配的其他平台表。";
        case HOST_PLATFORM_NO_TABLE:return "缺少平台表，也未找到资源匹配的补表来源。";
        case HOST_PLATFORM_NO_IMAGES:return "平台表指向的图片资源缺失，启动可能黑屏。";
        default:return "无法确认资源匹配情况，请结合启动日志检查。";
        }
    }
    // -1 back, 1 font, 2/6 platform forward/backward, 4/5 mesh, 7 BG alpha.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+6)%7;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%7;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=rowsTop&&y<rowsTop+7*rowPitch){row=(y-rowsTop)/rowPitch;choose=true;}}
        if(row==1&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return pressed&SCE_CTRL_LEFT?6:2;
        if(row==2&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return pressed&SCE_CTRL_LEFT?5:4;
        if(row==3&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 7;
        if(choose)return row==0?1:row==4?8:row==5?9:-1;
        return 0;
    }
    void prepare(const char* id)const {
        for(auto text:{"字体设置","启动方式","Vita（默认）","Windows","Switch","Android","iOS","PS4","返回游戏选择",
            "E-mote Mesh","1.0（默认）","0.8","0.6","0.4",
            "OGV 预载与缓存","CPU 图片缓存压缩","忽略背景透明度","开启","关闭（默认）",
            "○ 进入"})ui_prepare(text,kMenuBodySize);
        ui_prepare("当前游戏设置",kMenuTitleSize);
        for(auto text:{"数值越低网格越简化；1.0 保留原始精度。",
            "仅对当前游戏生效，切换后下次启动使用。",
            "默认 Vita；选择对应平台的启动脚本和配置。",
            "保存失败，仍使用原设置。","↑↓ 选择   ○ 确认   ←→ 调整   × 返回"})ui_prepare(text,kMenuNoteSize);
        for(auto text:backgroundAlphaHelp)ui_prepare(text,kMenuNoteSize);
        menu_prepare(id,kMenuBodySize);
        ui_prepare("检查中",kMenuNoteSize);ui_prepare(resource_detail(),kMenuNoteSize);
        for(unsigned i=0;i<kGamePlatformCount;++i){
            ui_prepare(overviewLabels[i],kMenuNoteSize);
            ui_prepare(resource_label(resources[i]),kMenuNoteSize);
        }
    }
    void draw(const char* id)const {
        rect(0,0,960,544,0x101b2bff);ui_text(160,60,kMenuTitleSize,"当前游戏设置");menu_text(160,100,kMenuBodySize,id);
        const char* labels[]={"字体设置","启动方式","E-mote Mesh","忽略背景透明度","CPU 图片缓存压缩","OGV 预载与缓存","返回游戏选择"};
        for(int i=0;i<7;i++){float y=rowsTop+i*rowPitch;rect(160,y,640,rowHeight,i==row?0x286482ff:0x1c2838ff);ui_setting_text(182,y+27,kMenuBodySize,labels[i]);}
        const unsigned selected=platform<kGamePlatformCount?platform:0;
        ui_setting_text(650,rowsTop+27,kMenuBodySize,"○ 进入");ui_setting_text(430,rowsTop+rowPitch+27,kMenuBodySize,kGamePlatformLabels[selected]);
        ui_setting_text(650,rowsTop+rowPitch+27,kMenuNoteSize,resourcesPending?"检查中":resource_label(resources[selected]),resource_color(resources[selected]));
        ui_setting_text(600,rowsTop+2*rowPitch+27,kMenuBodySize,emote_mesh_label(emoteMesh));
        ui_setting_text(600,rowsTop+3*rowPitch+27,kMenuBodySize,ignoreBackgroundAlpha?"开启":"关闭（默认）");
        ui_setting_text(650,rowsTop+4*rowPitch+27,kMenuBodySize,"○ 进入");
        ui_setting_text(650,rowsTop+5*rowPitch+27,kMenuBodySize,"○ 进入");
        rect(160,overviewTop,640,65,0x142233ff);
        for(unsigned i=0;i<kGamePlatformCount;++i){
            const float x=170+(i%3)*214,y=overviewBaseline+(i/3)*overviewPitch;
            ui_text(x,y,kMenuNoteSize,overviewLabels[i],0xb5c4d4ff);
            ui_text(x+100,y,kMenuNoteSize,resourcesPending?"检查中":resource_label(resources[i]),resource_color(resources[i]));
        }
        ui_text(160,435,kMenuNoteSize,failed?"保存失败，仍使用原设置。":"仅对当前游戏生效，切换后下次启动使用。",failed?0xff8080ff:0xb5c4d4ff);
        if(row==3){for(unsigned i=0;i<3;++i)ui_text(160,458+i*24,kMenuNoteSize,backgroundAlphaHelp[i],0xb5c4d4ff);}
        else ui_text(160,469,kMenuNoteSize,row==1?resource_detail():row==2?"数值越低网格越简化；1.0 保留原始精度。":"",0xb5c4d4ff);
        ui_text(160,536,kMenuNoteSize,"↑↓ 选择   ○ 确认   ←→ 调整   × 返回");
    }
};
}
