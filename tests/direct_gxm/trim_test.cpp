#include "quad_trim.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace direct;
using Quad=std::array<Vertex,4>;
static Quad quad(float x,float y,float w,float h,float u=0,float v=0,float uw=1,float vh=1){
    return {{{x,y,u,v,1,1,1,1},{x+w,y,u+uw,v,1,1,1,1},{x,y+h,u,v+vh,1,1,1,1},{x+w,y+h,u+uw,v+vh,1,1,1,1}}};
}
static double alpha(const std::vector<uint8_t>& p,float u,float v){
    const double x=u*64-.5,y=v*48-.5;int ix=int(std::floor(x)),iy=int(std::floor(y));
    double result=0;for(int j=0;j<2;j++)for(int i=0;i<2;i++)result+=
        p[(std::clamp(iy+j,0,47)*64+std::clamp(ix+i,0,63))*4+3]*
        (i?x-ix:1-x+ix)*(j?y-iy:1-y+iy);
    return result;
}
int main(){
    std::vector<uint8_t> p(64*48*4,0);
    for(int y=13;y<34;y++)for(int x=21;x<44;x++)p[(y*64+x)*4+3]=(x+y)%2?255:1;
    AlphaBounds bounds;bounds.include(p.data(),64,0,0,64,48);
    assert(bounds.left==21&&bounds.right==44&&bounds.top==13&&bounds.bottom==34);
    unsigned compared=0;
    for(float flip:{-1.f,1.f})for(float scale:{.5f,1.f,2.7f})for(float offset:{0.f,.125f,.9f}){
        Quad original=quad(40+offset,20+offset,128*scale,96*scale,flip<0?1.f:0.f,0,flip,1),q=original;
        bool trimmed=false;assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::Draw&&trimmed);
        assert(screen_area(q.data())<screen_area(original.data())*.3);
        for(int y=0;y<300;y++)for(int x=0;x<420;x++){
            const float xx=x+.5f,yy=y+.5f;
            if(xx<original[0].x||xx>=original[1].x||yy<original[0].y||yy>=original[2].y)continue;
            float u=original[0].u+(xx-original[0].x)/(original[1].x-original[0].x)*(original[1].u-original[0].u);
            float v=original[0].v+(yy-original[0].y)/(original[2].y-original[0].y)*(original[2].v-original[0].v);
            if(xx<q[0].x||xx>=q[1].x||yy<q[0].y||yy>=q[2].y)assert(alpha(p,u,v)<1e-5);
            else {
                float nu=q[0].u+(xx-q[0].x)/(q[1].x-q[0].x)*(q[1].u-q[0].u);
                float nv=q[0].v+(yy-q[0].y)/(q[2].y-q[0].y)*(q[2].v-q[0].v);
                assert(std::abs(alpha(p,u,v)-alpha(p,nu,nv))<.02);
            }++compared;
        }
    }
    bool trimmed=false;Quad q=quad(10,10,64,48);for(auto& v:q)v.a=0;
    assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::ZeroAlpha);
    q=quad(-100,-100,20,20);assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::Outside);
    q=quad(10,10,64,48,-1,0,2,1);assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::Draw&&!trimmed);
    q=quad(10,10,64,48);q[1].r=.5;assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::Draw&&!trimmed);
    q=quad(10,10,64,48);q[1].y+=5;assert(trim_quad(q.data(),bounds,64,48,trimmed)==QuadResult::Draw&&!trimmed);
    q=quad(10,10,64,48);AlphaBounds imported;assert(trim_quad(q.data(),imported,64,48,trimmed)==QuadResult::Draw&&!trimmed);
    AlphaBounds empty;std::vector<uint8_t> dynamic(64*48*4,0);empty.include(dynamic.data(),64,0,0,64,48);
    assert(trim_quad(q.data(),empty,64,48,trimmed)==QuadResult::Empty);
    dynamic[(47*64+63)*4+3]=255;empty.include(dynamic.data(),64,63,47,1,1);
    assert(trim_quad(q.data(),empty,64,48,trimmed)==QuadResult::Draw&&trimmed);
    p[3]=255;bounds.include(p.data(),64,0,0,1,1);assert(bounds.left==0&&bounds.top==0&&bounds.right==44);
    std::printf("PASS %u bilinear samples; zero/outside/empty, flip, subpixel, scale, CLAMP, gradient, shear, import, partial update\n",compared);
}
