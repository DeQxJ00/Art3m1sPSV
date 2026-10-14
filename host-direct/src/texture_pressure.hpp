#pragma once
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace direct {
// A failed scene texture has priority over reproducible render caches. One
// recovery per pressure episode; elapsed time alone never permits another one.
struct TexturePressure {
    size_t requested=0;
    uint64_t lastFailure=0;
    bool pending=false, suppressed=false;
    void failed(size_t bytes,uint64_t now){
        requested=std::max(requested,bytes);lastFailure=now;
        if(!suppressed)pending=true;
    }
    bool recover(){
        if(!pending)return false;
        pending=false;suppressed=true;return true;
    }
    bool allow_cache(size_t free,uint64_t now){
        if(pending)return false;
        if(suppressed&&now-lastFailure>=2000000&&free>=requested+8*1024*1024){
            suppressed=false;requested=0;
        }
        return !suppressed;
    }
};
// Offscreen recovery runs only between fenced frames. Repeated failures with
// unchanged capacity must not evict caches or retry allocations every frame.
struct OffscreenPressure {
    size_t requested=0,blockedFree=0;
    uint64_t lastFailure=0;
    bool pending=false,blocked=false;
    bool request(size_t bytes,uint64_t now){
        if(lastFailure&&now-lastFailure>2000000)blocked=false;
        if(bytes>requested)blocked=false;
        const bool first=!pending&&!blocked;
        requested=std::max(requested,bytes);lastFailure=now;pending=true;
        return first;
    }
    bool take(size_t free){
        if(!pending)return false;
        pending=false;
        return !blocked||(free>blockedFree&&free-blockedFree>=256*1024);
    }
    void finish(size_t free){blocked=true;blockedFree=free;}
};

}
