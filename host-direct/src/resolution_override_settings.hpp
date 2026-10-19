#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"
#include "game_platform.hpp"
namespace direct {
inline std::string resolution_override_path(const std::string& directory,const std::string& id,bool table){
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,table?".force-table-size":".force-ini-size");return path;
}
inline bool load_resolution_override(const std::string& directory,const std::string& id,bool table){
    bool enabled=false;load_debug_settings(resolution_override_path(directory,id,table),enabled,false);return enabled;
}
inline bool save_resolution_override(const std::string& directory,const std::string& id,bool table,bool enabled){
    auto path=resolution_override_path(directory,id,table);return !path.empty()&&save_debug_settings(path,enabled);
}
inline void override_ini_resolution(std::string& ini,std::string_view platform){
    auto section=ini_section(ini,platform);if(section.empty())return;
    ini.insert(size_t(section.data()-ini.data())+section.size(),"\nWIDTH=960\nHEIGHT=540\n");
}
}
