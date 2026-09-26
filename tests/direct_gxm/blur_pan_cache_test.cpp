#include "blur_pan_cache.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace direct;
int main(){
    float uv[4];unsigned checks=0;
    for(float dx:{-400.f,-125.5f,0.f,39.f,400.f})for(float dy:{-200.f,-75.25f,0.f,99.f,200.f}){
        assert(blur_pan_crop(dx,dy,.02f,uv));
        // Source texels under every screen point must be the same world point
        // after subtracting the translation; include fractional subpixel pans.
        for(float x:{0.f,123.5f,960.f})for(float y:{0.f,56.25f,544.f}){
            assert(std::abs((uv[0]+x/1920)*1920-480-(x-dx))<.0002f);
            assert(std::abs((uv[1]+y/1088)*1088-272-(y-dy))<.0002f);++checks;
        }
    }
    assert(!blur_pan_crop(481,0,0,uv));assert(!blur_pan_crop(0,273,0,uv));
    assert(!blur_pan_crop(0,250,.03f,uv));assert(!blur_pan_crop(0,0,.3f,uv));
    assert(!blur_pan_crop(std::numeric_limits<float>::quiet_NaN(),0,0,uv));
    BlurPanKey key;key.count=5;key.texture=7;key.content=1;key.shaders=1;
    for(unsigned i=0;i<key.shape.size();++i){auto next=key;next.shape[i]=1;assert(!(next==key));}
    auto next=key;assert(next==key);++next.texture;assert(!(next==key));
    next=key;++next.content;assert(!(next==key));next=key;++next.shaders;assert(!(next==key));
    next=key;++next.count;assert(!(next==key));next=key;++next.blend;assert(!(next==key));
    for(unsigned i=0;i<5;++i){next=key;++next.passes[i].program;assert(!(next==key));
        for(unsigned j=0;j<128;++j){next=key;next.passes[i].values[j]=.5f;assert(!(next==key));++checks;}}
    std::printf("blur pan: %u coordinate and invalidation checks passed\n",checks);
}
