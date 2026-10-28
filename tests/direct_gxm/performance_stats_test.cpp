#include "../../host-direct/src/performance_stats.hpp"
#include <cassert>
#include <cmath>
int main(){
    direct::PerformanceWindow w;w.reset(1000);
    for(unsigned i=1;i<=25;++i){
        const bool ready=w.add(1000+i*20000,2000,4000,14000,{i,600,3,3000,7000,2,1048576,1500,4});
        assert(ready==(i==25));
    }
    assert(std::abs(w.sample.fps-50)<0.001&&w.sample.frameMs==20&&w.sample.maxMs==20);
    assert(w.sample.mediaMs==2&&w.sample.logicMs==4&&w.sample.presentMs==14);
    assert(w.sample.draws==13&&w.sample.peakDraws==25&&w.sample.triangles==600&&w.sample.passes==3);
    assert(w.sample.gpuWaitMs==3&&w.sample.queueWaitMs==7&&w.sample.uploadMs==1.5);
    assert(w.sample.uploads==2&&w.sample.uploadMiB==1&&w.sample.shared==4);
    // Include a long stall between frames, even if its work is outside the
    // measured media/logic/present sections. Keep the prior display until ready.
    assert(!w.add(521000,1000,2000,17000,{2,12,1}));
    assert(w.sample.fps==50);
    assert(w.add(1001000,1000,2000,17000,{4,24,2}));
    assert(w.sample.fps==4&&w.sample.maxMs==480&&w.sample.draws==3);
    assert(w.sample.gpuWaitMs==0&&w.sample.queueWaitMs==0&&w.sample.uploads==0&&w.sample.shared==0);
    assert(!w.add(1001000,0,0,0,{}));
    // Closing/reopening a menu discards stale samples and elapsed pause time.
    w.reset(9000000);assert(w.sample.fps==0&&w.frames==0);
    assert(w.add(9500000,0,0,500000,{1,2,1})&&w.sample.fps==2);
}
