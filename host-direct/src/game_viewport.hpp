#pragma once
#include <algorithm>
#include <cmath>

namespace direct {
struct GameViewport {
    float x=0,y=0,w=960,h=544;
    bool full() const { return x==0&&y==0&&w==960&&h==544; }
    bool touch(int rawX,int rawY,unsigned stageW,unsigned stageH,int& outX,int& outY) const {
        const float px=rawX*.5f,py=rawY*.5f;
        if(!stageW||!stageH||px<x||py<y||px>=x+w||py>=y+h)return false;
        outX=std::clamp(int((px-x)*stageW/w),0,int(stageW)-1);
        outY=std::clamp(int((py-y)*stageH/h),0,int(stageH)-1);
        return true;
    }
};
inline GameViewport game_viewport(unsigned w,unsigned h){
    if(!w||!h)return {};
    // Preserve the existing native widescreen path (540/544-line resources).
    const float aspect=float(w)/h;
    if(std::abs(aspect-960.f/544)<.015f)return {};
    const float scale=std::min(960.f/w,544.f/h);
    const float width=w*scale,height=h*scale;
    return {(960-width)*.5f,(544-height)*.5f,width,height};
}
}
