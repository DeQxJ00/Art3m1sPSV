#include "opaque_tiles_row_major.hpp"
#include <cassert>
#include <random>
#include <cstdio>
int main(){
    std::mt19937 rng(934);
    unsigned cases=0;
    for(unsigned w:{1u,3u,4u,15u,16u,63u,64u,65u,127u,257u,960u,1920u})
    for(unsigned h:{1u,3u,63u,64u,65u,193u,1080u})
    for(unsigned mode=0;mode<5;++mode){
        std::vector<uint8_t> bytes(size_t(w)*h*4+3,255);
        auto* p=bytes.data()+3; // Deliberately unaligned, no readable tail guard.
        for(size_t i=0;i<size_t(w)*h;++i){
            p[i*4]=rng();p[i*4+1]=rng();p[i*4+2]=rng();
            if(mode==1)p[i*4+3]=0;
            if(mode==2&&rng()%997==0)p[i*4+3]=254;
            if(mode==3&&(i%w)%64==63)p[i*4+3]=1;
        }
        if(mode==4)p[(size_t(w)*h-1)*4+3]=254;
        const auto before=bytes;
        direct::OpaqueTiles expected,actual;
        expected.build(p,w,h);direct::build_opaque_tiles_row_major(actual,p,w,h);
        assert(actual.cells==expected.cells);assert(bytes==before);
        ++cases;
    }
    direct::OpaqueTiles invalid;direct::build_opaque_tiles_row_major(invalid,nullptr,64,64);
    assert(!invalid.covers(0,0,1,1));
    printf("row-major tile proof: %u exact-map cases passed\n",cases);
}
