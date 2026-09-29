#pragma once
#include "cpu3_setting.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct LauncherSettingsMenu {
    int row=0;bool shaderReadable=true,shaderFailed=false;bool cacheReadable=true,cacheFailed=false;bool debugReadable=true,debugFailed=false;bool logReadable=true,logFailed=false;
    // -1 back, 1 CPU3, 2 clocks, 3 conversion, 4 compilation, 5 cache HUD, 6 startup debug display, 7 logging.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+7)%8;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%8;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=106&&y<418){row=(y-106)/39;choose=true;}}
        if((row==0||row==2||row==3||row==4||row==5||row==6)&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return row+1;
        if(choose)return row==1?2:-1;
        return 0;
    }
    void prepare()const{
        menu_prepare("启动器设置",kMenuTitleSize);
        for(auto text:{"Debug 调试","Debug 缓存浮窗","日志",
            "CapUnlocker","超频设置","返回游戏选择",
            "开","关","读取失败",
            "保存失败","○ 进入","Shader 自动转换",
            "Shader 自动编译"})menu_prepare(text,kMenuBodySize);
        for(auto text:{"默认关闭：开启后显示开篇 Shader 自检画面。","重启应用生效；关闭时仍在后台校验渲染能力。","默认关闭：在游戏画面右侧显示缓存占用。",
            "每半秒刷新；数值单位 MiB，次数为本次游戏累计。","默认开启：记录到 host.log，方便排查问题。","关闭后不生成或轮换日志；重启应用生效。",
            "默认关闭：内置覆盖 51 个文件，一般无需开启。","外置效果请准备转换好的 Cg 和配套参数文件。","只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。",
            "HLSL → Cg；修改后下次进入游戏生效。","Cg → GXP；修改后下次进入游戏生效。","CapUnlocker：允许后台任务使用第四个 CPU 核心。",
            "切换后重启应用生效，需要已安装并启用插件。","超频设置：全局、OGV、特效平移、E-mote。","↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回"})menu_prepare(text,kMenuNoteSize);

    }
    void draw(const Cpu3Setting& cpu3,const ShaderSettings& shader,bool cacheEnabled,bool debugEnabled,bool logEnabled)const{
        rect(0,0,960,544,0x101b2bff);menu_text(160,70,kMenuTitleSize,"启动器设置");
        const char* labels[]={"CapUnlocker","超频设置","Shader 自动转换","Shader 自动编译","Debug 缓存浮窗","Debug 调试","日志","返回游戏选择"};
        for(int i=0;i<8;i++){float y=106+i*39;rect(160,y,640,35,i==row?0x286482ff:0x1c2838ff);
            menu_text(182,y+29,kMenuBodySize,labels[i]);}
        const char* status=!cpu3.readable?"读取失败":cpu3.failed?"保存失败":cpu3.next?"开":"关";
        menu_text(656,135,kMenuBodySize,status,cpu3.failed||!cpu3.readable?0xff8080ff:0xffffffff);
        menu_text(656,174,kMenuBodySize,"○ 进入");
        for(int i=0;i<2;++i)menu_text(656,213+i*39,kMenuBodySize,!shaderReadable?"读取失败":shaderFailed?"保存失败":(i?shader.compile:shader.convert)?"开":"关",shaderFailed||!shaderReadable?0xff8080ff:0xffffffff);
        menu_text(656,291,kMenuBodySize,!cacheReadable?"读取失败":cacheFailed?"保存失败":cacheEnabled?"开":"关",cacheFailed||!cacheReadable?0xff8080ff:0xffffffff);
        menu_text(656,330,kMenuBodySize,!debugReadable?"读取失败":debugFailed?"保存失败":debugEnabled?"开":"关",debugFailed||!debugReadable?0xff8080ff:0xffffffff);
        menu_text(656,369,kMenuBodySize,!logReadable?"读取失败":logFailed?"保存失败":logEnabled?"开":"关",logFailed||!logReadable?0xff8080ff:0xffffffff);
        if(row==0){menu_text(160,442,kMenuNoteSize,"CapUnlocker：允许后台任务使用第四个 CPU 核心。");
            menu_text(160,476,kMenuNoteSize,"切换后重启应用生效，需要已安装并启用插件。");}
        else if(row==1)menu_text(160,442,kMenuNoteSize,"超频设置：全局、OGV、特效平移、E-mote。");
        else if(row==2||row==3){menu_text(160,442,kMenuNoteSize,"默认关闭：内置覆盖 51 个文件，一般无需开启。");
            menu_text(160,468,kMenuNoteSize,row==2?"外置效果请准备转换好的 Cg 和配套参数文件。":"只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。");
            menu_text(160,490,kMenuNoteSize,row==2?"HLSL → Cg；修改后下次进入游戏生效。":"Cg → GXP；修改后下次进入游戏生效。");}
        else if(row==4){menu_text(160,442,kMenuNoteSize,"默认关闭：在游戏画面右侧显示缓存占用。");
            menu_text(160,468,kMenuNoteSize,"每半秒刷新；数值单位 MiB，次数为本次游戏累计。");}
        else if(row==5){menu_text(160,442,kMenuNoteSize,"默认关闭：开启后显示开篇 Shader 自检画面。");
            menu_text(160,468,kMenuNoteSize,"重启应用生效；关闭时仍在后台校验渲染能力。");}
        else if(row==6){menu_text(160,442,kMenuNoteSize,"默认开启：记录到 host.log，方便排查问题。");
            menu_text(160,468,kMenuNoteSize,"关闭后不生成或轮换日志；重启应用生效。");}
        menu_text(160,514,kMenuNoteSize,"↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回");
    }
};
}
