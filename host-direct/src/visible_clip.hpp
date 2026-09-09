#pragma once
#include "quad_trim.hpp"

namespace direct {
// The render target rejects all samples outside [0,960)x[0,544).
// A clip enclosing the visible bounding box cannot reject any sprite sample,
// including for rotated/sheared quads. Keep malformed/empty clips unchanged.
inline bool clip_redundant_on_target(const Vertex* q,const float* clip) {
    if(!clip)return true;
    for(unsigned i=0;i<4;++i)
        if(!std::isfinite(clip[i])||!std::isfinite(q[i].x)||!std::isfinite(q[i].y))return false;
    if(clip[0]>=clip[2]||clip[1]>=clip[3])return false;
    float left=q[0].x,right=left,top=q[0].y,bottom=top;
    for(unsigned i=1;i<4;++i){
        left=std::min(left,q[i].x);right=std::max(right,q[i].x);
        top=std::min(top,q[i].y);bottom=std::max(bottom,q[i].y);
    }
    left=std::max(left,0.f);right=std::min(right,960.f);
    top=std::max(top,0.f);bottom=std::min(bottom,544.f);
    return left<right&&top<bottom&&left>=clip[0]&&top>=clip[1]&&right<=clip[2]&&bottom<=clip[3];
}
}
