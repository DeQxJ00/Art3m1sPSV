#pragma once
#include "cpu_affinity.h"
namespace direct {
struct Cpu3Setting {
    bool next=false,readable=false,failed=false;
    void open(){int enabled=0;readable=host_cpu_affinity_read_setting(&enabled)>=0;
        next=enabled!=0;failed=false;}
    void toggle(){
        if(!readable){open();return;}
        failed=host_cpu_affinity_save_setting(!next)<0;
        if(!failed)next=!next;
    }
    const char* label() const {
        if(!readable)return "CapUnlocker：读取失败   △ 重试";
        if(failed)return "CapUnlocker：保存失败   △ 重试";
        return next?"CapUnlocker：开   △ 切换   重启应用后生效（需已启用插件）"
                   :"CapUnlocker：关   △ 切换   重启应用后生效（需已启用插件）";
    }
};
}
