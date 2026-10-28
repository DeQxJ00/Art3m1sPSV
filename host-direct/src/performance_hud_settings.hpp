#pragma once
#include "cache_hud_settings.hpp"
namespace direct {
inline constexpr const char* performanceHudPath="ux0:data/art3m1s-gxm/performance-hud.txt";
inline bool performanceHudEnabled=false,performanceHudReadable=true;
inline void load_performance_hud(){performanceHudReadable=load_cache_hud(performanceHudPath,performanceHudEnabled);}
inline bool toggle_performance_hud(){
    if(!save_cache_hud(performanceHudPath,!performanceHudEnabled))return false;
    performanceHudEnabled=!performanceHudEnabled;performanceHudReadable=true;return true;
}
}
