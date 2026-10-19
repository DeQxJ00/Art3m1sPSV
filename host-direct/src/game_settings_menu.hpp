#pragma once
#include "gpu.hpp"
#include "emote_mesh_settings.hpp"
#include "emote_backend_settings.hpp"
#include "background_alpha_settings.hpp"
#include "audio_fade_settings.hpp"
#include "auto_platform_tables_settings.hpp"
#include "resolution_override_settings.hpp"
#include "large_image_settings.hpp"
#include "game_platform.hpp"
#include "../../host/files.h"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct GameSettingsMenu {
    // Keep menu rows, touch bounds and the platform summary on one layout.
    static constexpr int rowsTop=112,rowPitch=32,rowHeight=30,rowBaseline=25,rowCount=13,visibleRows=7;
    static constexpr int overviewTop=344,overviewBaseline=367,overviewPitch=28;
    int row=0;unsigned platform=0;bool failed=false;
    unsigned emoteMesh=kDefaultEmoteMesh;
    unsigned emoteGrid=kDefaultEmoteGrid;
    unsigned emoteBackend=kDefaultEmoteBackend;
    bool ignoreBackgroundAlpha=false;
    bool audioFade=false;bool autoPlatformTables=true;bool forceIni=false,forceTable=false;
    int firstRow()const{return row/visibleRows*visibleRows;}
    bool largeImageCompression=true;unsigned largeImageThreshold=30;
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
    // -1 back, 1 font, 2/6 platform, 4/5 mesh, 7 BG alpha, 8 image compression, 9 OGV, 10 audio, 11 backend, 12 PSB cache.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch) {
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+rowCount-1)%rowCount;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%rowCount;
        if(pressed&SCE_CTRL_RTRIGGER){row=firstRow()+visibleRows>=rowCount?0:firstRow()+visibleRows;return 0;}
        if(pressed&SCE_CTRL_LTRIGGER){row=firstRow()?firstRow()-visibleRows:7;return 0;}
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(y>=40&&y<76&&x>=650&&x<800){row=x<725?(firstRow()?firstRow()-visibleRows:7):(firstRow()+visibleRows>=rowCount?0:firstRow()+visibleRows);return 0;}
            if(x>=160&&x<800&&y>=rowsTop&&y<rowsTop+visibleRows*rowPitch){int next=firstRow()+(y-rowsTop)/rowPitch;if(next<rowCount){row=next;choose=true;}}}
        if(row==1&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return pressed&SCE_CTRL_LEFT?6:2;
        if(row==2&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return pressed&SCE_CTRL_LEFT?5:4;
        if(row==3&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 7;
        if(row==6&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 10;
        if(row==7&&kRecoveredEmoteAvailable&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 11;
        if(row==9&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return 13;
        if((row==10||row==11)&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return row==10?14:15;
        if(choose)return row==0?1:row==4?8:row==5?9:row==8?12:row==12?-1:0;
        return 0;
    }
    void prepare(const char* id)const {
        for(auto text:{"字体设置","启动方式","Vita（默认）","Windows","Switch","Android","iOS","PS4","返回游戏选择","PSB 模型缓存",
            "emote引擎","旧emote引擎","新emote引擎（默认）","8×8","6×6","4×4（默认）","2×2","1×1","E-mote 精度","1.0（默认）","0.8","0.6","0.4",
            "音频渐入渐出","OGV 预载与缓存","图片压缩","忽略背景透明度","开启","关闭（默认）",
            "强制 INI 分辨率","强制 TBL 分辨率","是","否（默认）","L  ‹","›  R","自动补表","开启（默认）","关闭","○ 进入"})ui_prepare(text,kMenuBodySize);
        ui_prepare("缺少平台表时自动补齐，下次启动生效。",kMenuNoteSize);
        ui_prepare("当前游戏设置",kMenuTitleSize);
        ui_prepare("仅覆盖当前平台 WIDTH / HEIGHT 为 960×540。",kMenuNoteSize);
        ui_prepare("仅覆盖 game_width / game_height，保留 game_scale。",kMenuNoteSize);
        ui_prepare("8×8 保留细节；低档减少计算，1×1 / 2×2 会明显简化弯曲。",kMenuNoteSize);
        for(auto text:{"数值越低网格越简化；1.0 保留原始精度。",
            "新旧引擎分别保存精度；切换后重新进入游戏生效。",
            "当前安装包不包含新emote引擎，请安装支持新引擎的版本。",
            "切换后重新启动游戏生效；出现兼容问题可切回旧引擎。",
            "仅对当前游戏生效，切换后下次启动使用。",
            "默认 Vita；选择对应平台的启动脚本和配置。",
            "保存失败，仍使用原设置。","↑↓ 选择   ○ 确认   ←→ 调整   L/R 翻页   × 返回"})ui_prepare(text,kMenuNoteSize);
        ui_prepare("停止或切换音频时添加短渐变，默认关闭。",kMenuNoteSize);
        ui_prepare("游戏脚本指定的渐变不受此开关影响。",kMenuNoteSize);
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
        const char* labels[]={"字体设置","启动方式","E-mote 精度","忽略背景透明度","图片压缩","OGV 预载与缓存","音频渐入渐出","emote引擎","PSB 模型缓存","自动补表","强制 INI 分辨率","强制 TBL 分辨率","返回游戏选择"};
        ui_setting_text(650,60,kMenuBodySize,"L  ‹");ui_setting_text(735,60,kMenuBodySize,"›  R");
        const unsigned selected=platform<kGamePlatformCount?platform:0;
        for(int i=firstRow();i<rowCount&&i<firstRow()+visibleRows;i++){
            float y=rowsTop+(i-firstRow())*rowPitch;
            rect(160,y,640,rowHeight,i==row?0x286482ff:0x1c2838ff);
            ui_setting_text(182,y+rowBaseline,kMenuBodySize,labels[i]);
            const char* value=nullptr;
            switch(i){
            case 0:case 4:case 5:case 8:value="○ 进入";break;
            case 1:value=kGamePlatformLabels[selected];break;
            case 2:value=effective_emote_backend(emoteBackend)==2?emote_grid_label(emoteGrid):emote_mesh_label(emoteMesh);break;
            case 3:value=ignoreBackgroundAlpha?"开启":"关闭（默认）";break;
            case 6:value=audioFade?"开启":"关闭（默认）";break;
            case 7:value=emote_backend_label(emoteBackend);break;
            case 9:value=autoPlatformTables?"开启（默认）":"关闭";break;
            case 10:value=forceIni?"是":"否（默认）";break;
            case 11:value=forceTable?"是":"否（默认）";break;
            }
            if(value)ui_setting_text(i==7?500:590,y+rowBaseline,kMenuBodySize,value);
        }
        rect(160,overviewTop,640,65,0x142233ff);
        for(unsigned i=0;i<kGamePlatformCount;++i){
            const float x=170+(i%3)*214,y=overviewBaseline+(i/3)*overviewPitch;
            ui_text(x,y,kMenuNoteSize,overviewLabels[i],0xb5c4d4ff);
            ui_text(x+100,y,kMenuNoteSize,resourcesPending?"检查中":resource_label(resources[i]),resource_color(resources[i]));
        }
        ui_text(160,435,kMenuNoteSize,failed?"保存失败，仍使用原设置。":"仅对当前游戏生效，切换后下次启动使用。",failed?0xff8080ff:0xb5c4d4ff);
        if(row==7){ui_text(160,458,kMenuNoteSize,kRecoveredEmoteAvailable?"切换后重新启动游戏生效；出现兼容问题可切回旧引擎。":"当前安装包不包含新emote引擎，请安装支持新引擎的版本。",0xb5c4d4ff);
            if(kRecoveredEmoteAvailable)ui_text(160,482,kMenuNoteSize,"新旧引擎分别保存精度；切换后重新进入游戏生效。",0xb5c4d4ff);}
        else if(row==10){ui_text(160,469,kMenuNoteSize,"仅覆盖当前平台 WIDTH / HEIGHT 为 960×540。",0xb5c4d4ff);}
        else if(row==11){ui_text(160,469,kMenuNoteSize,"仅覆盖 game_width / game_height，保留 game_scale。",0xb5c4d4ff);}
        else if(row==9){ui_text(160,469,kMenuNoteSize,"缺少平台表时自动补齐，下次启动生效。",0xb5c4d4ff);}
        else if(row==3){for(unsigned i=0;i<3;++i)ui_text(160,458+i*24,kMenuNoteSize,backgroundAlphaHelp[i],0xb5c4d4ff);}
        else if(row==6){ui_text(160,458,kMenuNoteSize,"停止或切换音频时添加短渐变，默认关闭。",0xb5c4d4ff);
            ui_text(160,482,kMenuNoteSize,"游戏脚本指定的渐变不受此开关影响。",0xb5c4d4ff);}
        else ui_text(160,469,kMenuNoteSize,row==1?resource_detail():row==2?(effective_emote_backend(emoteBackend)==2?"8×8 保留细节；低档减少计算，1×1 / 2×2 会明显简化弯曲。":"数值越低网格越简化；1.0 保留原始精度。"):"",0xb5c4d4ff);
        ui_text(160,536,kMenuNoteSize,"↑↓ 选择   ○ 确认   ←→ 调整   L/R 翻页   × 返回");
    }
};
}
