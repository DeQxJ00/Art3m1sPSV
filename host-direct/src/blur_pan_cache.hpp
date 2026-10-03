#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace direct {
// A failed optional cache is retried only after real free-space growth or a
// different effect episode. Continuous pans cannot create a timed retry loop.
struct EffectPanAdmission {
    static constexpr uint32_t headroom=4*1024*1024,granule=256*1024;
    uint64_t lastUse=0;
    uint32_t blockedFree=0;
    bool blocked=false;
    bool allow(uint64_t now,uint32_t free){
        if(lastUse&&now-lastUse>2000000)blocked=false;
        lastUse=now;
        return !blocked||(free>blockedFree&&free-blockedFree>=granule);
    }
    void deny(uint32_t free){blocked=true;blockedFree=free;}
    void ready(){blocked=false;}
    static uint32_t storage(unsigned w,unsigned h){return ((uint64_t(w)*h*4+granule-1)&~uint64_t(granule-1))*2;}
    static uint32_t deficit(uint32_t storage,uint32_t free){return free>=storage+headroom?0:storage+headroom-free;}
};
struct EffectPanCreateResult { bool ready; int32_t cdramError=0; };
struct EffectPanAllocation {
    bool ready=false;
    uint32_t requested=0;
    size_t reclaimed=0;
    unsigned retries=0;
    unsigned headroomReclaims=0;
    uint32_t remainingDeficit=0;
    int failedSlot=-1;
    int32_t cdramError=0;
};
// Total free CDRAM does not describe its largest contiguous block. Recover a
// real block-allocation failure by evicting a bounded amount of cold residency.
// Keep successful surfaces during recovery; never retry GXM setup failures or
// retry without reclaiming anything. The caller cleans up on final failure.
template<class Free,class Create,class Reclaim>
EffectPanAllocation allocate_effect_pan_pair(uint32_t storage,unsigned present,
                                            Free freeBytes,Create create,Reclaim reclaim){
    EffectPanAllocation result;
    constexpr uint32_t recoveryStep=4*1024*1024;
    constexpr unsigned maxRetries=4;
    constexpr unsigned maxHeadroomReclaims=4;
    const auto missingBytes=[&](){return (2-unsigned(bool(present&1))-unsigned(bool(present&2)))*(storage/2);};
    result.remainingDeficit=EffectPanAdmission::deficit(missingBytes(),freeBytes());
    while(result.remainingDeficit){
        if(result.headroomReclaims==maxHeadroomReclaims)return result;
        const auto needed=result.remainingDeficit;
        result.requested+=needed;
        const auto released=reclaim(needed);
        result.reclaimed+=released;++result.headroomReclaims;
        // Retired texture bytes and the kernel's free-space measurement can
        // differ. Re-measure and reclaim the remaining deficit in this bounded
        // preparation pass, rather than blocking the whole pan after one try.
        result.remainingDeficit=EffectPanAdmission::deficit(missingBytes(),freeBytes());
        if(result.remainingDeficit&&!released)return result;
    }
    for(;;){
        for(unsigned i=0;i<2;++i){
            if(present&(1u<<i))continue;
            const auto attempt=create(i);
            if(attempt.ready){present|=1u<<i;continue;}
            result.failedSlot=int(i);result.cdramError=attempt.cdramError;
            break;
        }
        if(present==3){result.ready=true;return result;}
        if(result.cdramError>=0||result.retries==maxRetries)return result;
        const auto released=reclaim(recoveryStep);
        result.requested+=recoveryStep;result.reclaimed+=released;
        if(!released)return result;
        ++result.retries;
    }
}
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
