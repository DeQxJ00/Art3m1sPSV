#pragma once
#include "gpu.hpp"
#include "update_check.hpp"
namespace direct {
inline void prepare_update_notice(const UpdateCheck& check){
    const auto title=std::string(ui_translate("有更新："))+check.tag();
    ui_prepare(title.c_str(),16);
}
inline void draw_update_notice(const UpdateCheck& check){
    rect(580,464,348,22,0x142638ee);
    const auto title=std::string(ui_translate("有更新："))+check.tag();
    ui_text(592,481,16,title.c_str());
}
}
