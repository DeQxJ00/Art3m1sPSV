#pragma once
#include "quad_trim.hpp"
namespace direct {
// Only a certified opaque, unmasked rectangle may erase earlier color draws.
// This renderer has no depth/stencil side effects. Never use a bounding box for
// rotated quads: it does not prove that every target sample is covered.
inline bool covers_target_opaque(const Vertex* q,bool opaque,unsigned blend,unsigned variant){
    if(!opaque||blend!=2||variant)return false;
    for(unsigned i=0;i<4;++i)
        if(q[i].a!=1||!std::isfinite(q[i].x)||!std::isfinite(q[i].y)||
           !std::isfinite(q[i].u)||!std::isfinite(q[i].v)||
           !std::isfinite(q[i].r)||!std::isfinite(q[i].g)||!std::isfinite(q[i].b))return false;
    if(q[0].x!=q[2].x||q[1].x!=q[3].x||q[0].y!=q[1].y||q[2].y!=q[3].y)return false;
    return std::min(q[0].x,q[1].x)<=0&&std::max(q[0].x,q[1].x)>=960&&
           std::min(q[0].y,q[2].y)<=0&&std::max(q[0].y,q[2].y)>=544;
}
}
