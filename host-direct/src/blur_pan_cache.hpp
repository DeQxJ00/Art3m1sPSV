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
// One bounded result cache for all verified translation-invariant filters.
// Source positions are relative to the first layer; moving an entire stack
// keeps the key, moving one expression layer or changing its pixels does not.
struct EffectPanSource {
    uint64_t texture=0,content=0;
    std::array<float,16> values{}; // matrix, relative XY, quad, UV, tint
    unsigned blend=0;
    bool operator==(const EffectPanSource& s)const{return texture==s.texture&&content==s.content&&values==s.values&&blend==s.blend;}
};
struct EffectPanKey {
    std::array<EffectPanSource,32> sources{};
    std::array<BlurPanKey::Pass,16> passes{};
    std::array<unsigned,16> kinds{};
    uint64_t shaders=0;
    unsigned sourceCount=0,count=0;
    bool half=false;
    float sx=1,sy=1;
    bool operator==(const EffectPanKey& k)const{return sources==k.sources&&passes==k.passes&&kinds==k.kinds
        &&shaders==k.shaders&&sourceCount==k.sourceCount&&count==k.count&&half==k.half&&sx==k.sx&&sy==k.sy;}
};
struct EffectPanGeometry {
    unsigned width=1200,height=680;
    float extentX=1200,extentY=680;
    static EffectPanGeometry legacy(){return {960,544,1920,1088};}
    bool crop(float dx,float dy,float radiusX,float radiusY,float* uv)const{
        uv[0]=((extentX-960)*.5f-dx)/extentX;uv[1]=((extentY-544)*.5f-dy)/extentY;
        uv[2]=uv[0]+960/extentX;uv[3]=uv[1]+544/extentY;
        return std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(radiusX)&&std::isfinite(radiusY)
            &&radiusX>=0&&radiusY>=0&&uv[0]>=radiusX/extentX&&uv[1]>=radiusY/extentY
            &&uv[2]<=1-radiusX/extentX&&uv[3]<=1-radiusY/extentY;
    }
};
inline bool blur_pan_crop(float dx,float dy,float guard,float* uv){
    uv[0]=.25f-dx/1920.f;uv[1]=.25f-dy/1088.f;uv[2]=uv[0]+.5f;uv[3]=uv[1]+.5f;
    return std::isfinite(dx)&&std::isfinite(dy)&&std::isfinite(guard)&&guard>=0&&guard<.25f
        &&uv[0]>=guard&&uv[1]>=guard&&uv[2]<=1-guard&&uv[3]<=1-guard;
}
}
