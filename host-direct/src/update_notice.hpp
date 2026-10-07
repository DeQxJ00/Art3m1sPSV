#pragma once
#include "gpu.hpp"
#include "update_check.hpp"
namespace direct {
inline void prepare_update_notice(const UpdateCheck& check){
    const auto title=std::string(ui_translate("有更新："))+check.tag();
    ui_prepare(title.c_str(),kMenuNoteSize);
}
inline void draw_update_notice(const UpdateCheck& check){
    rect(600,10,348,38,0x142638ee);
    const auto title=std::string(ui_translate("有更新："))+check.tag();
    ui_text(614,36,kMenuNoteSize,title.c_str());
}
}
