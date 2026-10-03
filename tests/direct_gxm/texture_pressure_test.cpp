#include "../../host-direct/src/texture_pressure.hpp"
#include <cassert>
int main(){
    constexpr size_t MiB=1024*1024;
    direct::TexturePressure p;
    assert(p.allow_cache(0,0));
    p.failed(9*MiB,100);p.failed(10*MiB,101);
    assert(p.requested==10*MiB&&!p.allow_cache(32*MiB,200));
    assert(p.recover()&&!p.recover());
    for(unsigned i=0;i<1000;++i){p.failed(9*MiB,200+i);assert(!p.recover());}
    assert(!p.allow_cache(17*MiB,10000000));
    assert(!p.allow_cache(32*MiB,1200));
    assert(p.allow_cache(18*MiB,3000000));
    assert(p.requested==0);
    p.failed(2*MiB,4000000);assert(p.recover());
}
