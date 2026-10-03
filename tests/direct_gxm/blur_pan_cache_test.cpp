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
    for(auto geometry:{EffectPanGeometry{},EffectPanGeometry::legacy()}){
        for(float dx:{-30.f,0.f,.5f,30.f})for(float dy:{-20.f,0.f,.25f,20.f}){
            assert(geometry.crop(dx,dy,12,9,uv));
            for(float x:{0.f,123.5f,960.f})for(float y:{0.f,56.25f,544.f}){
                assert(std::abs((uv[0]+x/geometry.extentX)*geometry.extentX-(geometry.extentX-960)*.5f-(x-dx))<.0003f);
                assert(std::abs((uv[1]+y/geometry.extentY)*geometry.extentY-(geometry.extentY-544)*.5f-(y-dy))<.0003f);++checks;
            }
        }
        assert(!geometry.crop(0,0,geometry.extentX,0,uv));
        assert(!geometry.crop(INFINITY,0,0,0,uv));
        assert(!geometry.crop(0,0,-1,0,uv));
    }
    EffectPanKey stack;stack.sourceCount=2;stack.count=2;stack.kinds[0]=1;stack.kinds[1]=2;
    auto altered=stack;assert(altered==stack);
    for(unsigned i=0;i<2;++i){
        altered=stack;altered.sources[i].texture++;assert(!(altered==stack));
        altered=stack;altered.sources[i].content++;assert(!(altered==stack));
        for(auto j=0u;j<16;++j){altered=stack;altered.sources[i].values[j]=1;assert(!(altered==stack));++checks;}
        altered=stack;altered.passes[i].values[0]=.5f;assert(!(altered==stack));
    }
    altered=stack;altered.kinds[0]=6;assert(!(altered==stack));
    altered=stack;altered.half=true;assert(!(altered==stack));
    altered=stack;altered.sx=.5;assert(!(altered==stack));
    std::printf("blur pan: %u coordinate and invalidation checks passed\n",checks);
}
