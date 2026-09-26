#pragma once
#include <array>
#include <cmath>
#include <cstdint>
namespace direct {
// A 960x544 texture represents a 1920x1088 screen-space rectangle. Its middle
// half is the reference viewport; translating selects a different half-sized
// crop. The guard excludes texels affected by the cache's own clamp boundary.
struct BlurPanKey {
    uint64_t texture=0,content=0,shaders=0;
    std::array<float,12> shape{}; // linear matrix, quad, UV, stage scale
    struct Pass {unsigned program=0;std::array<float,128> values{};
        bool operator==(const Pass& p)const{return program==p.program&&values==p.values;}};
    std::array<Pass,16> passes{};
    unsigned count=0,blend=0;
    bool operator==(const BlurPanKey& k)const{
        return texture==k.texture&&content==k.content&&shaders==k.shaders&&shape==k.shape
            &&count==k.count&&blend==k.blend&&passes==k.passes;
    }
};
inline bool blur_pan_crop(float dx,float dy,float guard,float* uv){
    uv[0]=.25f-dx/1920.f;uv[1]=.25f-dy/1088.f;uv[2]=uv[0]+.5f;uv[3]=uv[1]+.5f;
    return std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(guard)&&guard>=0&&guard<.25f
        &&uv[0]>=guard&&uv[1]>=guard&&uv[2]<=1-guard&&uv[3]<=1-guard;
}
}
