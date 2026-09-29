#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"

namespace direct {
inline std::string toolbar_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".toolbar");
    return path;
}
inline bool load_toolbar_settings(const std::string& directory,const std::string& id) {
    bool hidden=false;load_debug_settings(toolbar_settings_path(directory,id),hidden);return hidden;
}
inline bool save_toolbar_settings(const std::string& directory,const std::string& id,bool hidden) {
    const auto path=toolbar_settings_path(directory,id);
    return !path.empty()&&save_debug_settings(path,hidden);
}
inline std::string volume_bar_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".volume-bar");
    return path;
}
inline bool load_volume_bar_settings(const std::string& directory,const std::string& id) {
    bool hidden=false;load_debug_settings(volume_bar_settings_path(directory,id),hidden);return hidden;
}
inline bool save_volume_bar_settings(const std::string& directory,const std::string& id,bool hidden) {
    const auto path=volume_bar_settings_path(directory,id);
    return !path.empty()&&save_debug_settings(path,hidden);
}
}
