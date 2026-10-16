#include "recovered_clip_region.hpp"
#include <cassert>
#include <limits>
#include <random>
#include <cstdio>
int main(){
    unsigned r[4]={0,0,959,543};
    assert(!direct::recovered_clip_region(nullptr,r));
    float invalid[]={0,0,std::numeric_limits<float>::infinity(),1};
    assert(!direct::recovered_clip_region(invalid,r));
    float portrait[]={0,355,190,540};
    assert(direct::recovered_clip_region(portrait,r));
    assert(r[0]==0&&r[1]==352&&r[2]==191&&r[3]==543);
    std::mt19937 rng(71);unsigned accepted=0;
    for(unsigned trial=0;trial<10000;++trial){
        float c[4];for(float&v:c)v=float(int(rng()%200000)-50000)/100;
        if(!direct::recovered_clip_region(c,r))continue;
        ++accepted;assert(r[0]<=r[2]&&r[1]<=r[3]&&r[2]<960&&r[3]<544);
        assert(!(r[0]%32)&&!(r[1]%32)&&r[2]%32==31&&r[3]%32==31);
        // Every exact stencil sample, including fractional/edge clips, survives.
        for(unsigned y=0;y<544;++y)for(unsigned x=0;x<960;++x)
            if(x+.5f>=c[0]&&x+.5f<c[2]&&y+.5f>=c[1]&&y+.5f<c[3])
                assert(x>=r[0]&&x<=r[2]&&y>=r[1]&&y<=r[3]);
    }
    printf("PASS %u conservative clip regions\n",accepted);
}
