#pragma once
#include "cpu3_setting.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>

namespace direct {
struct LauncherSettingsMenu {
    int row=0;bool shaderReadable=true,shaderFailed=false;bool cacheReadable=true,cacheFailed=false;
    // -1 back, 1 CPU3, 2 clocks, 3 conversion, 4 compilation, 5 cache HUD.
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_START))return -1;
        if(pressed&SCE_CTRL_UP)row=(row+5)%6;
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%6;
        bool choose=pressed&SCE_CTRL_CIRCLE;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=160&&x<800&&y>=106&&y<394){row=(y-106)/48;choose=true;}}
        if((row==0||row==2||row==3||row==4)&&(choose||(pressed&(SCE_CTRL_LEFT|SCE_CTRL_RIGHT))))return row+1;
        if(choose)return row==1?2:-1;
        return 0;
    }
    void prepare()const{
        for(auto s:{"缓存浮窗","默认关闭：在游戏画面右侧显示缓存占用。","每半秒刷新；数值单位 MiB，次数为本次游戏累计。","启动器设置","CapUnlocker","超频设置","返回游戏选择","开","关",
            "读取失败","保存失败","○ 进入","Shader 自动转换","Shader 自动编译",
            "默认关闭：内置覆盖 51 个文件，一般无需开启。",
            "外置效果请准备转换好的 Cg 和配套参数文件。",
            "只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。",
            "HLSL → Cg；修改后下次进入游戏生效。",
            "Cg → GXP；修改后下次进入游戏生效。",
            "CapUnlocker：允许后台任务使用第四个 CPU 核心。",
            "切换后重启应用生效，需要已安装并启用插件。",
            "超频设置：分别调整全局和 OGV 动画期间的频率。",
            "↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回"})menu_prepare(s,24);
    }
    void draw(const Cpu3Setting& cpu3,const ShaderSettings& shader,bool cacheEnabled)const{
        rect(0,0,960,544,0x101b2bff);menu_text(160,70,24,"启动器设置");
        const char* labels[]={"CapUnlocker","超频设置","Shader 自动转换","Shader 自动编译","缓存浮窗","返回游戏选择"};
        for(int i=0;i<6;i++){float y=106+i*48;rect(160,y,640,42,i==row?0x286482ff:0x1c2838ff);
            menu_text(182,y+29,24,labels[i]);}
        const char* status=!cpu3.readable?"读取失败":cpu3.failed?"保存失败":cpu3.next?"开":"关";
        menu_text(656,135,24,status,cpu3.failed||!cpu3.readable?0xff8080ff:0xffffffff);
        menu_text(656,183,24,"○ 进入");
        for(int i=0;i<2;++i)menu_text(656,231+i*48,24,!shaderReadable?"读取失败":shaderFailed?"保存失败":(i?shader.compile:shader.convert)?"开":"关",shaderFailed||!shaderReadable?0xff8080ff:0xffffffff);
        menu_text(656,327,24,!cacheReadable?"读取失败":cacheFailed?"保存失败":cacheEnabled?"开":"关",cacheFailed||!cacheReadable?0xff8080ff:0xffffffff);
        if(row==0){menu_text(160,442,24,"CapUnlocker：允许后台任务使用第四个 CPU 核心。");
            menu_text(160,476,24,"切换后重启应用生效，需要已安装并启用插件。");}
        else if(row==1)menu_text(160,442,24,"超频设置：分别调整全局和 OGV 动画期间的频率。");
        else if(row==2||row==3){menu_text(160,418,24,"默认关闭：内置覆盖 51 个文件，一般无需开启。");
            menu_text(160,450,24,row==2?"外置效果请准备转换好的 Cg 和配套参数文件。":"只有 Cg 需开启编译；匹配的 GXP 缓存可直接用。");
            menu_text(160,482,24,row==2?"HLSL → Cg；修改后下次进入游戏生效。":"Cg → GXP；修改后下次进入游戏生效。");}
        else if(row==4){menu_text(160,430,24,"默认关闭：在游戏画面右侧显示缓存占用。");
            menu_text(160,468,24,"每半秒刷新；数值单位 MiB，次数为本次游戏累计。");}
        menu_text(160,514,24,"↑↓ 选择   ○ 确认   ←→ 切换开关   × 返回");
    }
};
}
