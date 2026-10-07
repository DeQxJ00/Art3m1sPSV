#pragma once
#include "gpu.hpp"
#include "game_library.hpp"
#include <cstdio>

namespace direct {
struct ResourceNotice {
    static constexpr const char* path="ux0:data/art3m1s-gxm/resource-notice-seen.txt";
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
    static constexpr const char* title="游戏资源准备";
    static constexpr const char* lines[]={
        "请先用配套的转换工具 Art3m1sPsvPortTool",
        "缩放、转换图片和视频资源，然后将处理好的资源放进：",
        "ux0:/data/art3m1s-gxm/games/",
        "一个子文件夹对应一个游戏。",
        "详细步骤请看 GitHub 的 README：",
        "https://github.com/DeQxJ00/Art3m1sPSV",
    };
    void prepare()const{
        ui_prepare(title,kMenuTitleSize);
        for(const auto* line:lines)ui_prepare(line,kMenuNoteSize);
        ui_prepare("○ 确认",kMenuBodySize);
        if(failed)ui_prepare("保存失败，请重试",kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);
        ui_text(48,72,kMenuTitleSize,title);
        for(unsigned i=0;i<6;++i)ui_text(48,142+46*i,kMenuNoteSize,lines[i]);
        if(failed)ui_text(48,420,kMenuNoteSize,"保存失败，请重试",0xff8080ff);
        rect(320,452,320,52,0x286482ff);
        ui_text(416,487,kMenuBodySize,"○ 确认");
    }
};
}
