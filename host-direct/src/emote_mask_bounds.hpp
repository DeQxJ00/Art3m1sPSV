#pragma once
#include "quad_trim.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace direct {
// Bounds of the vertices actually submitted to a freshly cleared color target.
// Any non-native draw invalidates the proof; mask draws do not enlarge it.
struct EmoteMaskBounds {
    bool nativeOnly=false,seen=false;
    float left=0,top=0,right=0,bottom=0;
    void reset(){nativeOnly=true;seen=false;}
    void invalidate(){nativeOnly=false;}
    void include(const Vertex* vertices,size_t count,bool native){
        if(!native){invalidate();return;}
        if(!nativeOnly)return;
        for(size_t i=0;i<count;++i){
            const float x=vertices[i].x,y=vertices[i].y;
            if(!std::isfinite(x)||!std::isfinite(y)){invalidate();return;}
            if(!seen){left=right=x;top=bottom=y;seen=true;}
            else {left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);}
        }
    }
    bool rectangle(float* out) const {
        if(!nativeOnly||!seen)return false;
        // Include neighboring pixels so subpixel coverage and point-sampled
        // render-target coordinates retain the complete original edge.
        out[0]=std::clamp(std::floor(left)-1,0.f,960.f);
        out[1]=std::clamp(std::floor(top)-1,0.f,544.f);
        out[2]=std::clamp(std::ceil(right)+1,0.f,960.f);
        out[3]=std::clamp(std::ceil(bottom)+1,0.f,544.f);
        return out[0]<out[2]&&out[1]<out[3];
    }
};
}
