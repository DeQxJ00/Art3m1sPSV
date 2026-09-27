#pragma once
#include "builtin_effects.hpp"
#include <cmath>
namespace direct {
inline bool simple_emote_material(const BuiltinEffects& e,bool hasMask){
    if(hasMask||e.flags[0]!=0||e.flags[1]!=0||e.flags[2]!=0||e.flags[3]!=1||e.wipe[2]!=0)
        return false;
    if(e.modelClip[0]!=-1.e30f||e.modelClip[1]!=-1.e30f||e.modelClip[2]!=1.e30f||e.modelClip[3]!=1.e30f)
        return false;
    for(unsigned i=0;i<16;++i)
        if(!std::isfinite(e.corners[i])||e.corners[i]!=e.corners[i%4])return false;
    return true;
}
}
