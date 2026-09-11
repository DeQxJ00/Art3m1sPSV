#include "../../host-direct/src/loading_ps_guard.hpp"
#include <cassert>
#include <iostream>
#include <string>
static int locks,unlocks,lockResult,unlockResult;static std::string reason;
static int lock(){++locks;return lockResult;}static int unlock(){++unlocks;return unlockResult;}
static void report(const char* r,int){reason=r;}
static void reset(){locks=unlocks=lockResult=unlockResult=0;reason.clear();}
int main(){
    reset();{direct::LoadingPsGuard g(lock,unlock,report);g.begin(10);g.begin(20);g.poll(100);assert(locks==1&&unlocks==0);
        g.finish("first-game-frame");g.poll(200);assert(!g.active()&&unlocks==1&&reason=="first-game-frame");g.poll(300);}
    assert(unlocks==1);
    reset();lockResult=-1;{direct::LoadingPsGuard g(lock,unlock,report);g.begin(0);g.finish("load-failed");g.poll(1);assert(!g.active());}assert(unlocks==0);
    reset();{direct::LoadingPsGuard g(lock,unlock,report);g.begin(0);g.finish("load-failed");unlockResult=-1;g.poll(1);assert(g.active());
        g.poll(999999);assert(unlocks==1);unlockResult=0;g.poll(1000001);assert(!g.active()&&unlocks==2);}
    reset();{direct::LoadingPsGuard g(lock,unlock,report);g.begin(10);g.poll(10+g.timeoutUs-1);assert(g.active());g.poll(10+g.timeoutUs);assert(!g.active()&&reason=="timeout");}
    reset();{direct::LoadingPsGuard g(lock,unlock,report);g.begin(0);}assert(unlocks==1&&reason=="destroy");
    std::cout<<"loading PS guard: ownership, first-frame release, lock failure, unlock retry, timeout and cleanup passed\n";
}
