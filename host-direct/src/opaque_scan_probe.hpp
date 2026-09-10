#pragma once
#include "opaque_tiles.hpp"
namespace direct {
// CPU-only test, before gameplay. No GXM state, assets, shader or clock changes.
template<class Clock,class Log>
bool opaque_scan_probe(Clock clock,Log log){
    bool ok=true;
    for(unsigned w:{960u,1920u}){
        const unsigned h=w==960?540:1080;
        std::vector<uint8_t> storage(size_t(w)*h*4+3);
        auto* p=storage.data()+3; // Also exercise unaligned SIMD source reads.
        for(size_t i=0;i<size_t(w)*h;++i){
            p[i*4]=uint8_t(i*17);p[i*4+1]=uint8_t(i*31);p[i*4+2]=uint8_t(i*7);p[i*4+3]=255;
        }
        for(unsigned pattern=0;pattern<3;++pattern){
            if(pattern==1)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
                p[(size_t(y)*w+x)*4+3]=(x<64||x>=w-64||y<64||y>=h-64)?0:255;
            if(pattern==2)for(size_t i=0;i<size_t(w)*h;++i)p[i*4+3]=uint8_t(i*41);
            for(unsigned round=0;round<2;++round){
                OpaqueTiles reference,fast;
                uint64_t old_us=0,new_us=0;
                auto old_run=[&]{auto start=clock();reference.build_reference(p,w,h);old_us=clock()-start;};
                auto new_run=[&]{auto start=clock();fast.build(p,w,h);new_us=clock()-start;};
                if(round&1){new_run();old_run();}else{old_run();new_run();}
                bool equal=reference.cells==fast.cells;ok=ok&&equal;
                log("[opaque-scan-bench] width=%u height=%u pattern=%u round=%u old_us=%llu new_us=%llu equal=%d",
                    w,h,pattern,round,(unsigned long long)old_us,(unsigned long long)new_us,int(equal));
            }
        }
    }
    return ok;
}
}
