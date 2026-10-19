#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"

namespace direct {
inline std::string auto_platform_tables_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".auto-platform-tables");
    return path;
}
inline bool load_auto_platform_tables_settings(const std::string& directory,const std::string& id) {
    bool enabled=true;
    load_debug_settings(auto_platform_tables_settings_path(directory,id),enabled,true);
    return enabled;
}
inline bool save_auto_platform_tables_settings(const std::string& directory,const std::string& id,bool enabled) {
    const auto path=auto_platform_tables_settings_path(directory,id);
    return !path.empty()&&save_debug_settings(path,enabled);
}
}
