#include "blur_pan_cache.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace direct;
int main(){
    EffectPanAdmission admission;
    constexpr unsigned M=1024*1024;
    assert(EffectPanAdmission::storage(1200,680)==6656*1024);
    assert(EffectPanAdmission::deficit(6656*1024,2*M)==8704*1024);
    assert(EffectPanAdmission::deficit(6656*1024,11*M)==0);
    assert(admission.allow(1,2*M));admission.deny(2*M);
    for(uint64_t now=10000;now<20000000;now+=10000)assert(!admission.allow(now,2*M));
    assert(!admission.allow(20000000,2*M+128*1024));
    assert(admission.allow(20010000,2*M+256*1024));
    admission.deny(2*M+256*1024);
    assert(!admission.allow(20020000,2*M));
    assert(admission.allow(23000000,2*M)); // Another scene after inactivity.
    admission.ready();assert(admission.allow(23000001,0));
    // Enough total space, but the second contiguous allocation is blocked.
    // Recovery must preserve the first surface rather than create it again.
    {
        unsigned calls[2]{},evictions=0;uint32_t free=14*M;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[&]{return free;},
            [&](unsigned slot){++calls[slot];if(slot==1&&!evictions)return EffectPanCreateResult{false,-1};
                free-=3328*1024;return EffectPanCreateResult{true};},
            [&](uint32_t requested){assert(requested==4*M);++evictions;free+=4*M;return size_t(4*M);});
        assert(result.ready&&calls[0]==1&&calls[1]==2&&evictions==1&&result.retries==1);
        assert(result.failedSlot==1&&result.cdramError==-1&&result.reclaimed==4*M);
    }
    // Permanently fragmented/pinned space: bounded work, no repeated first slot.
    {
        unsigned calls[2]{},evictions=0;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[]{return 14*M;},
            [&](unsigned slot){++calls[slot];return EffectPanCreateResult{slot==0,slot?-1:0};},
            [&](uint32_t requested){++evictions;return size_t(requested);});
        assert(!result.ready&&calls[0]==1&&calls[1]==5&&evictions==4);
        assert(result.retries==4&&result.requested==16*M&&result.reclaimed==16*M);
    }
    // No cold textures, or a non-allocation GXM error: do not spin/evict more.
    for(int32_t error:{0,-1}){
        unsigned calls=0,evictions=0;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[]{return 14*M;},
            [&](unsigned){++calls;return EffectPanCreateResult{false,error};},
            [&](uint32_t){++evictions;return size_t(0);});
        assert(!result.ready&&calls==1&&evictions==unsigned(error<0)&&result.retries==0);
    }
    // Ordinary deficit reclamation still precedes allocations; never allocate
    // if the reclaimable set cannot leave the required rendering headroom.
    for(bool available:{false,true}){
        unsigned calls=0;uint32_t free=3*M;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[&]{return free;},
            [&](unsigned){++calls;return EffectPanCreateResult{true};},
            [&](uint32_t requested){assert(requested==7680*1024);if(available)free+=requested;return available?size_t(requested):0;});
        assert(result.ready==available&&calls==(available?2u:0u)&&result.retries==0);
    }
    // A partially resident pair only needs space/allocation for the missing one.
    {
        unsigned calls=0;
        const auto result=allocate_effect_pan_pair(6656*1024,1,[]{return 8*M;},
            [&](unsigned slot){assert(slot==1);++calls;return EffectPanCreateResult{true};},
            [](uint32_t){assert(false);return size_t(0);});
        assert(result.ready&&calls==1&&result.requested==0);
    }
    // Device regression: 9 MiB free, 6.5 MiB pair plus 4 MiB headroom.
    // Reclaim reports 1.5 MiB, but the next free-space query only reports 10 MiB.
    // A coarse free counter models this discrepancy without assuming its cause.
    {
        uint32_t actualFree=9*M;unsigned creates=0,reclaims=0;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[&]{return actualFree/M*M;},
            [&](unsigned){++creates;actualFree-=3328*1024;return EffectPanCreateResult{true};},
            [&](uint32_t requested){
                assert(requested==(reclaims?512*1024:1536*1024));++reclaims;
                actualFree+=requested;return size_t(requested);
            });
        assert(result.ready&&creates==2&&reclaims==2&&result.headroomReclaims==2);
        assert(result.requested==2*M&&result.reclaimed==2*M&&!result.remainingDeficit);
        assert(result.retries==0&&actualFree>=EffectPanAdmission::headroom);
    }
    // Accounting may report retired bytes while actual space remains pinned.
    // Stop within one bounded preparation pass; never allocate below headroom.
    for(bool canReclaim:{false,true}){
        unsigned calls=0,reclaims=0;
        const auto result=allocate_effect_pan_pair(6656*1024,0,[]{return 9*M;},
            [&](unsigned){++calls;return EffectPanCreateResult{true};},
            [&](uint32_t bytes){++reclaims;return canReclaim?size_t(bytes):0;});
        assert(!result.ready&&!calls&&reclaims==(canReclaim?4u:1u));
        assert(result.remainingDeficit==1536*1024&&result.headroomReclaims==reclaims);
        assert(result.failedSlot==-1&&result.cdramError==0);
    }
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
    std::printf("blur pan: admission/recovery cases and %u coordinate and invalidation checks passed\n",checks);
}
