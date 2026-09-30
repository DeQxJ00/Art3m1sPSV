#include "../../host-direct/src/loading_progress.hpp"
#include <cassert>
int main(){
    direct::LoadingProgress p;p.begin(100);
    assert(p.fraction(0,0)==0);
    assert(p.fraction(8,4)==.25f); // Completed archives are not completed startup.
    p.advance(p.Engine,200);p.advance(p.Script,300);p.advance(p.FirstFrame,400);
    assert(p.fraction(100,100)==.75f);
    assert(!p.advance(p.Script,500)); // Later shader batches cannot rewind it.
    for(int i=0;i<240;i++)assert(!p.observe_frame(1,false)); // Clear-only startup.
    assert(p.pending&&p.emptyFrames==240);
    assert(!p.observe_frame(-1,true)); // FFI errors must not count as success.
    assert(!p.observe_frame(0,true));
    assert(p.observe_frame(1,true));assert(p.pending); // Queue/drain still pending.
    p.presented(600);assert(!p.pending&&p.fraction(0,0)==1.f);
    assert(!p.observe_frame(1,true)); // Subsequent black scenes never re-arm UI.
}
