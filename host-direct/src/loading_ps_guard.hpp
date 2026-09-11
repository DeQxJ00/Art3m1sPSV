#pragma once
#include <cstdint>

namespace direct {
// Owns exactly one successful lock. Calls remain on the runtime owner thread.
class LoadingPsGuard {
    int (*lock_)();int (*unlock_)();void (*report_)(const char*,int);
    bool held_=false,attempted_=false;uint64_t started_=0,lastAttempt_=0;
    const char* releaseReason_=nullptr;bool triedRelease_=false;
public:
    static constexpr uint64_t timeoutUs=120000000;
    LoadingPsGuard(int(*lock)(),int(*unlock)(),void(*report)(const char*,int)):
        lock_(lock),unlock_(unlock),report_(report){}
    LoadingPsGuard(const LoadingPsGuard&)=delete;
    LoadingPsGuard& operator=(const LoadingPsGuard&)=delete;
    ~LoadingPsGuard(){if(held_){int r=unlock_();report_("destroy",r);}}
    void begin(uint64_t now){
        if(attempted_)return;attempted_=true;started_=now;
        int r=lock_();held_=r>=0;report_("lock",r);
    }
    bool active() const {return held_;}
    void finish(const char* reason){if(held_&&!releaseReason_)releaseReason_=reason;}
    void poll(uint64_t now){
        if(!held_)return;
        // Fail open if a responsive loader never produces a frame. This is not
        // a watchdog for a blocked runtime thread; power controls remain alone.
        if(!releaseReason_&&now-started_>=timeoutUs)releaseReason_="timeout";
        if(!releaseReason_||(triedRelease_&&now-lastAttempt_<1000000))return;
        triedRelease_=true;lastAttempt_=now;
        int r=unlock_();report_(releaseReason_,r);if(r>=0)held_=false;
    }
};
}
