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
    p.uploaded(MiB);assert(p.requested==2*MiB);
    p.uploaded(2*MiB);assert(p.requested==0);
    assert(!p.allow_cache(8*MiB,4000001)); // Success does not bypass cooldown.
    assert(!p.allow_cache(7*MiB,6000000));
    assert(p.allow_cache(8*MiB,6000000));
    p.failed(8*MiB,7000000);assert(p.recover());p.uploaded(8*MiB);
    p.failed(4*MiB,7000001);assert(p.requested==4*MiB);
    assert(p.pending&&p.recover()); // Another atlas failed during cache cooldown.
    assert(!p.allow_cache(11*MiB,10000000));
    // Multiple equal-sized atlases can arrive together. Each successful upload
    // permits recovery of the next debt, never repeated recovery of the same one.
    direct::TexturePressure atlases;
    for(unsigned atlas=0;atlas<4;++atlas){
        atlases.failed(8*MiB,100+atlas);
        assert(atlases.recover());
        for(unsigned retry=0;retry<100;++retry){
            atlases.failed(8*MiB,200+retry);assert(!atlases.recover());
        }
        atlases.uploaded(8*MiB);
        assert(atlases.requested==0&&atlases.suppressed);
        assert(!atlases.allow_cache(32*MiB,1000));
    }
    direct::TexturePressure settled;
    settled.failed(9*MiB,100);settled.recover();
    assert(settled.cache_reclaim_bytes(5*MiB,3000000)==0); // Upload still owed.
    settled.uploaded(9*MiB);
    assert(settled.cache_reclaim_bytes(5*MiB,1000000)==0); // Cooldown.
    assert(settled.cache_reclaim_bytes(5*MiB,3000000)==3*MiB);
    for(unsigned i=0;i<1000;++i)assert(settled.cache_reclaim_bytes(5*MiB,3000000+i)==0);
    assert(settled.allow_cache(8*MiB,3000000));
    settled.failed(2*MiB,4000000);settled.recover();settled.uploaded(2*MiB);
    assert(settled.cache_reclaim_bytes(7*MiB,7000000)==MiB); // New episode.
    direct::TexturePressure changed;
    changed.capacity_changed();assert(!changed.pending);
    changed.failed(8*MiB,100);assert(changed.recover());
    for(unsigned i=0;i<1000;++i){changed.failed(8*MiB,200+i);assert(!changed.recover());}
    changed.capacity_changed();assert(changed.recover()); // Later scene retired a large texture.
    assert(!changed.recover());
    changed.uploaded(8*MiB);changed.capacity_changed();assert(!changed.pending);
    // Fragmented capacity: reclaim cold blocks until a real atlas probe fits.
    unsigned probes=0,reclaims=0;
    auto r=direct::recover_texture_block([&](){return ++probes==3;},[&](size_t bytes){assert(bytes==4*MiB);++reclaims;return bytes;});
    assert(r.ready&&probes==3&&reclaims==2&&r.reclaimed==8*MiB);
    probes=reclaims=0;
    r=direct::recover_texture_block([&](){++probes;return true;},[&](size_t){++reclaims;return MiB;});
    assert(r.ready&&probes==1&&reclaims==0);
    probes=reclaims=0;
    r=direct::recover_texture_block([&](){++probes;return false;},[&](size_t){++reclaims;return size_t(0);});
    assert(!r.ready&&probes==1&&reclaims==1);
    probes=reclaims=0;
    r=direct::recover_texture_block([&](){++probes;return false;},[&](size_t bytes){++reclaims;return bytes;});
    assert(!r.ready&&probes==33&&reclaims==32&&r.reclaimed==128*MiB);
    probes=reclaims=0;
    r=direct::recover_texture_block([&](){return ++probes==7;},[&](size_t bytes){++reclaims;return bytes;});
    assert(r.ready&&reclaims==6&&r.reclaimed==24*MiB); // More than four cold releases.
    direct::OffscreenPressure off;
    assert(!off.take(0));assert(off.request(2*MiB,10));assert(off.take(0));off.finish(0);
    for(unsigned i=0;i<1000;++i){assert(!off.request(2*MiB,20+i));assert(!off.take(0));}
    off.request(2*MiB,1100);assert(!off.take(128*1024));
    off.request(2*MiB,1200);assert(off.take(256*1024));off.finish(256*1024);
    assert(off.request(4*MiB,1300));assert(off.take(0));off.finish(0);
    assert(off.request(4*MiB,3000000));assert(off.take(0));
}
