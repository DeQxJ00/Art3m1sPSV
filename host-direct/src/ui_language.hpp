#pragma once
#include "log_settings.hpp"
#include <cstring>

namespace direct {
enum class UiLanguage { Chinese, English };
inline UiLanguage uiLanguage=UiLanguage::Chinese;
// UI preference only: never affects script locale, game paths or saved games.
inline bool load_ui_language(const std::string& path,bool* configured=nullptr) {
    bool english=false;
    const bool ok=load_log_settings(path,english);
    // log settings default to true; a missing language file defaults to Chinese.
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(configured)*configured=ok&&f!=nullptr;
    if(f)std::fclose(f);else english=false;
    uiLanguage=ok&&english?UiLanguage::English:UiLanguage::Chinese;
    return ok;
}
inline bool save_ui_language(const std::string& path,UiLanguage next) {
    if(!save_log_settings(path,next==UiLanguage::English))return false;
    uiLanguage=next;return true;
}
struct UiTranslation { const char* chinese; const char* english; };
inline constexpr UiTranslation uiTranslations[]={
#include "ui_translations.inc"
};
inline const char* ui_translate(const char* text) {
    if(!text||uiLanguage==UiLanguage::Chinese)return text;
    for(const auto& entry:uiTranslations)if(std::strcmp(entry.chinese,text)==0)return entry.english;
    return text;
}
}
