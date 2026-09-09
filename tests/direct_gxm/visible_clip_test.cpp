#include "visible_clip.hpp"
#include <array>
#include <cassert>
#include <limits>
#include <random>
#include <cstdio>
using namespace direct;
using Quad=std::array<Vertex,4>;
Quad rect(float x,float y,float w,float h){return {{{x,y},{x+w,y},{x,y+h},{x+w,y+h}}};}
int main(){
    float full[]={0,0,960,544},partial[]={100,50,500,400};
    auto q=rect(-200,-300,1300,1200);auto copy=q;
    assert(clip_redundant_on_target(q.data(),full));
    assert(!clip_redundant_on_target(q.data(),partial));
    q=rect(100,50,400,350);assert(clip_redundant_on_target(q.data(),partial));
    q=rect(99.9f,50,400,350);assert(!clip_redundant_on_target(q.data(),partial));
    q=rect(-20,100,200,100);float side[]={0,90,190,220};assert(clip_redundant_on_target(q.data(),side));
    float empty[]={0,0,0,544};assert(!clip_redundant_on_target(q.data(),empty));
    float bad[]={0,0,std::numeric_limits<float>::infinity(),544};assert(!clip_redundant_on_target(q.data(),bad));
    q[0].x=std::numeric_limits<float>::quiet_NaN();assert(!clip_redundant_on_target(q.data(),full));
    // Brute-force raster-center oracle on affine rotated/sheared/flipped quads.
    std::mt19937 rng(421);unsigned accepted=0,samples=0;
    auto random=[&](float a,float b){return a+(b-a)*(rng()%10001)/10000.f;};
    for(unsigned trial=0;trial<4000;++trial){
        float x=random(-500,1000),y=random(-400,600),a=random(-1000,1000),b=random(-600,600),c=random(-600,600),d=random(-600,600);
        q={{{x,y},{x+a,y+b},{x+c,y+d},{x+a+c,y+b+d}}};copy=q;
        float clip[]={random(-100,300),random(-100,200),random(400,1100),random(250,650)};
        if(trial%2==0){clip[0]=clip[1]=0;clip[2]=960;clip[3]=544;}
        if(!clip_redundant_on_target(q.data(),clip))continue;
        ++accepted;double det=double(a)*d-double(b)*c;if(std::abs(det)<.001)continue;
        for(int py=0;py<544;py+=5)for(int px=0;px<960;px+=5){
            double dx=px+.5-x,dy=py+.5-y,u=(dx*d-dy*c)/det,v=(dy*a-dx*b)/det;
            if(u<0||u>=1||v<0||v>=1)continue;
            assert(px+.5>=clip[0]&&py+.5>=clip[1]&&px+.5<clip[2]&&py+.5<clip[3]);++samples;
        }
        for(unsigned i=0;i<4;++i)assert(q[i].x==copy[i].x&&q[i].y==copy[i].y);
    }
    assert(accepted>500&&samples>10000);
    std::printf("PASS redundant clips: %u quads, %u covered sample checks; geometry unchanged\n",accepted,samples);
}
