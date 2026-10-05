#pragma once
#include "cpu3_setting.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct LauncherSettingsMenu {
    int row=0;bool shaderReadable=true,shaderFailed=false;bool cacheReadable=true,cacheFailed=false;bool debugReadable=true,debugFailed=false;bool logReadable=true,logFailed=false;bool languageFailed=false;
    // -1 back, 1 CPU3, 2 clocks, 3 conversion, 4 compilation, 5 cache HUD, 6 startup self-tests, 7 logging, 8 language.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+8)%9;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%9;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=106&&y<421){row=(y-106)/35;choose=true;}}
        if((row==0||row==2||row==3||row==4||row==5||row==6||row==7)&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return row+1;
        if(choose)return row==1?2:-1;
        return 0;
    }
    void prepare()const{
        ui_prepare("启动器设置",kMenuTitleSize);
        for(auto text:{"Debug 调试","Debug 缓存浮窗","日志",
            "CapUnlocker","超频设置","返回游戏选择",
            "开","关","读取失败",
            "保存失败","○ 进入","Shader 自动转换",
            "Shader 自动编译","语言 / Language","简体中文","English"})ui_prepare(text,kMenuBodySize);
        for(auto text:{"默认关闭：开启后运行并显示 Shader 自检。","重启应用生效；关闭时跳过启动自检。","默认关闭：在游戏画面右侧显示缓存占用。",
            "每半秒刷新；数值单位 MiB，次数为本次游戏累计。","默认开启：记录到 host.log，方便排查问题。","关闭后不生成或轮换日志；重启应用生效。",
            "默认关闭：内置覆盖 51 个文件，一般无需开启。","外置效果请准备转换好的 Cg 和配套参数文件。","只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。",
            "HLSL → Cg；修改后下次进入游戏生效。","Cg → GXP；修改后下次进入游戏生效。","CapUnlocker：允许后台任务使用第四个 CPU 核心。",
            "切换后重启应用生效，需要已安装并启用插件。","超频设置：全局、OGV、特效平移、E-mote。","↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回"})ui_prepare(text,kMenuNoteSize);

    }
    void draw(const Cpu3Setting& cpu3,const ShaderSettings& shader,bool cacheEnabled,bool debugEnabled,bool logEnabled)const{
        rect(0,0,960,544,0x101b2bff);ui_text(160,70,kMenuTitleSize,"启动器设置");
        const char* labels[]={"CapUnlocker","超频设置","Shader 自动转换","Shader 自动编译","Debug 缓存浮窗","Debug 调试","日志","语言 / Language","返回游戏选择"};
        for(int i=0;i<9;i++){float y=106+i*35;rect(160,y,640,32,i==row?0x286482ff:0x1c2838ff);
            ui_setting_text(182,y+29,kMenuBodySize,labels[i]);}
        const char* status=!cpu3.readable?"读取失败":cpu3.failed?"保存失败":cpu3.next?"开":"关";
        ui_setting_text(656,135,kMenuBodySize,status,cpu3.failed||!cpu3.readable?0xff8080ff:0xffffffff);
        ui_setting_text(656,170,kMenuBodySize,"○ 进入");
        for(int i=0;i<2;++i)ui_setting_text(656,205+i*35,kMenuBodySize,!shaderReadable?"读取失败":shaderFailed?"保存失败":(i?shader.compile:shader.convert)?"开":"关",shaderFailed||!shaderReadable?0xff8080ff:0xffffffff);
        ui_setting_text(656,275,kMenuBodySize,!cacheReadable?"读取失败":cacheFailed?"保存失败":cacheEnabled?"开":"关",cacheFailed||!cacheReadable?0xff8080ff:0xffffffff);
        ui_setting_text(656,310,kMenuBodySize,!debugReadable?"读取失败":debugFailed?"保存失败":debugEnabled?"开":"关",debugFailed||!debugReadable?0xff8080ff:0xffffffff);
        ui_setting_text(656,345,kMenuBodySize,!logReadable?"读取失败":logFailed?"保存失败":logEnabled?"开":"关",logFailed||!logReadable?0xff8080ff:0xffffffff);
        ui_setting_text(656,380,kMenuBodySize,languageFailed?"保存失败":uiLanguage==UiLanguage::English?"English":"简体中文");
        if(row==0){ui_text(160,442,kMenuNoteSize,"CapUnlocker：允许后台任务使用第四个 CPU 核心。");
            ui_text(160,476,kMenuNoteSize,"切换后重启应用生效，需要已安装并启用插件。");}
        else if(row==1)ui_text(160,442,kMenuNoteSize,"超频设置：全局、OGV、特效平移、E-mote。");
        else if(row==2||row==3){ui_text(160,442,kMenuNoteSize,"默认关闭：内置覆盖 51 个文件，一般无需开启。");
            ui_text(160,468,kMenuNoteSize,row==2?"外置效果请准备转换好的 Cg 和配套参数文件。":"只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。");
            ui_text(160,490,kMenuNoteSize,row==2?"HLSL → Cg；修改后下次进入游戏生效。":"Cg → GXP；修改后下次进入游戏生效。");}
        else if(row==4){ui_text(160,442,kMenuNoteSize,"默认关闭：在游戏画面右侧显示缓存占用。");
            ui_text(160,468,kMenuNoteSize,"每半秒刷新；数值单位 MiB，次数为本次游戏累计。");}
        else if(row==5){ui_text(160,442,kMenuNoteSize,"默认关闭：开启后运行并显示 Shader 自检。");
            ui_text(160,468,kMenuNoteSize,"重启应用生效；关闭时跳过启动自检。");}
        else if(row==6){ui_text(160,442,kMenuNoteSize,"默认开启：记录到 host.log，方便排查问题。");
            ui_text(160,468,kMenuNoteSize,"关闭后不生成或轮换日志；重启应用生效。");}
        ui_text(160,514,kMenuNoteSize,"↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回");
    }
};
}
