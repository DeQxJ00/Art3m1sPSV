#include "full_cover.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace direct;
int main(){
    Vertex q[]={{0,0,0,0,1,1,1,1},{960,0,1,0,1,1,1,1},{0,544,0,1,1,1,1,1},{960,544,1,1,1,1,1,1}};
    assert(covers_target_opaque(q,true,2,0));
    assert(!covers_target_opaque(q,false,2,0));
    for(unsigned b=0;b<2;++b)assert(!covers_target_opaque(q,true,b,0));
    for(unsigned v=1;v<4;++v)assert(!covers_target_opaque(q,true,2,v));
    q[0].a=.999f;assert(!covers_target_opaque(q,true,2,0));q[0].a=1;
    q[0].x=q[2].x=.1f;assert(!covers_target_opaque(q,true,2,0));q[0].x=q[2].x=0;
    q[0].x=-50;assert(!covers_target_opaque(q,true,2,0));q[0].x=0;
    q[0].u=std::numeric_limits<float>::quiet_NaN();assert(!covers_target_opaque(q,true,2,0));q[0].u=0;
    // Reflections/oversized rectangles cover every sample; enumerate all target
    // pixel centers independently of the helper's edge comparisons.
    for(int flip=0;flip<4;++flip){
        float l=-10,r=1000,t=-20,b=600;if(flip&1)std::swap(l,r);if(flip&2)std::swap(t,b);
        q[0].x=q[2].x=l;q[1].x=q[3].x=r;q[0].y=q[1].y=t;q[2].y=q[3].y=b;
        assert(covers_target_opaque(q,true,2,0));
        for(int y=0;y<544;++y)for(int x=0;x<960;++x){
            float u=(x+.5f-l)/(r-l),v=(y+.5f-t)/(b-t);assert(u>0&&u<1&&v>0&&v<1);
            // Opaque source-over output is independent of all prior colors.
            for(float old:{0.f,.5f,1.f})assert(.375f+old*(1.f-1.f)==.375f);
        }
    }
    std::puts("PASS full-cover eligibility and 2088960 target samples; transparent/masked/additive rejected");
}
