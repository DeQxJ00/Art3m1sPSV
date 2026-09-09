#include "opaque_tiles.hpp"
#include <cassert>
#include <random>
#include <cstdio>
int main(){
    const unsigned w=257,h=193;std::vector<uint8_t> pixels(w*h*4,255);
    direct::OpaqueTiles map;map.build(pixels.data(),w,h);
    assert(map.covers(0,0,1,1));
    pixels[3]=0;map.build(pixels.data(),w,h);
    assert(!map.covers(0,0,1,1));assert(map.covers(.4f,.4f,.8f,.8f));
    std::mt19937 rng(102);unsigned accepted=0;
    for(unsigned i=0;i<10000;++i){
        float u0=float(rng()%1001)/1000,v0=float(rng()%1001)/1000;
        float u1=float(rng()%1001)/1000,v1=float(rng()%1001)/1000;
        if(!map.covers(u0,v0,u1,v1))continue;++accepted;
        if(u0>u1)std::swap(u0,u1);if(v0>v1)std::swap(v0,v1);
        int x0=std::max(0,int(std::floor(u0*w))-1),x1=std::min(int(w)-1,int(std::ceil(u1*w))+1);
        int y0=std::max(0,int(std::floor(v0*h))-1),y1=std::min(int(h)-1,int(std::ceil(v1*h))+1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)assert(pixels[(y*w+x)*4+3]==255);
    }
    assert(!map.covers(-.01f,0,1,1));assert(!map.covers(NAN,0,1,1));
    map.clear();assert(!map.covers(.4f,.4f,.8f,.8f));
    printf("opaque tile proof: 10000 regions checked, %u accepted; invalidation passed\n",accepted);
}
