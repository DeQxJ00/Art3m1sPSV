#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"

namespace direct {
inline std::string audio_fade_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".audio-fade");
    return path;
}
inline bool load_audio_fade_settings(const std::string& directory,const std::string& id) {
    bool enabled=false;
    load_debug_settings(audio_fade_settings_path(directory,id),enabled,false);
    return enabled;
}
inline bool save_audio_fade_settings(const std::string& directory,const std::string& id,bool enabled) {
    const auto path=audio_fade_settings_path(directory,id);
    return !path.empty()&&save_debug_settings(path,enabled);
}
}
