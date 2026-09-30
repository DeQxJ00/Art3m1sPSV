#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"

namespace direct {
inline std::string background_alpha_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".background-alpha");
    return path;
}
inline bool load_background_alpha_settings(const std::string& directory,const std::string& id) {
    // Keep the existing preserve-alpha encoding for saved choices. Missing or
    // invalid settings now preserve transparency (ignore-alpha defaults off).
    bool preserve=true;
    load_debug_settings(background_alpha_settings_path(directory,id),preserve,true);
    return !preserve;
}
inline bool save_background_alpha_settings(const std::string& directory,const std::string& id,bool ignore) {
    const auto path=background_alpha_settings_path(directory,id);
    return !path.empty()&&save_debug_settings(path,!ignore);
}
}
