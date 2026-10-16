#pragma once
#include <algorithm>
#include <cmath>

namespace direct {
// Coarse rejection only: the original stencil still defines the exact edge.
// Expand by one pixel before rounding out to GXM's 32-pixel tiles.
inline bool recovered_clip_region(const float* clip,unsigned (&out)[4]) {
    if(!clip)return false;
    for(unsigned i=0;i<4;++i)if(!std::isfinite(clip[i]))return false;
    const float x0=std::max(0.f,clip[0]-1),y0=std::max(0.f,clip[1]-1);
    const float x1=std::min(960.f,clip[2]+1),y1=std::min(544.f,clip[3]+1);
    if(x0>=x1||y0>=y1)return false;
    out[0]=unsigned(std::floor(x0))&~31u;out[1]=unsigned(std::floor(y0))&~31u;
    out[2]=std::min(959u,(unsigned(std::ceil(x1))-1)|31u);
    out[3]=std::min(543u,(unsigned(std::ceil(y1))-1)|31u);
    return true;
}
}
