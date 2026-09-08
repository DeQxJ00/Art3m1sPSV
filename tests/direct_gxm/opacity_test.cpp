#include "texture_opacity.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
using namespace direct;
int main(){
    std::mt19937 rng(0x102c);unsigned checks=0;
    // Every channel/offset/short tail, including unaligned source pointers.
    for(unsigned offset=0;offset<16;++offset)for(unsigned n=1;n<130;++n){
        std::vector<uint8_t> storage(offset+n*4);auto* p=storage.data()+offset;
        for(unsigned i=0;i<n*4;++i)p[i]=uint8_t(rng());
        for(unsigned i=0;i<n;++i)p[i*4+3]=255;
        assert(pixels_are_opaque(p,n));++checks;
        for(unsigned i=0;i<n;++i){
            p[i*4+3]=uint8_t(rng()%255);assert(!pixels_are_opaque(p,n));++checks;
            p[i*4+3]=255;
        }
    }
    assert(!pixels_are_opaque(nullptr,0));
    // Large images must not read any pixels for optional certification.
    const uint8_t onePixel[]={255,255,255,255};
    assert(!certify_texture_opacity(onePixel,960*540));
    assert(!updated_opacity(true,onePixel,960,540,0,0,960,540));
    std::vector<uint8_t> boundary(1025*4,255);
    assert(certify_texture_opacity(boundary.data(),1024));
    assert(!certify_texture_opacity(boundary.data(),1025));
    // Repeated partial updates: a true certificate may never have a single
    // transparent texel, including writes to the last row/column.
    for(unsigned trial=0;trial<200;++trial){
        unsigned width=1+rng()%71,height=1+rng()%37;
        std::vector<uint8_t> p(size_t(width)*height*4,255);bool certified=true;
        for(unsigned step=0;step<100;++step){
            unsigned x=rng()%width,y=rng()%height,w=1+rng()%(width-x),h=1+rng()%(height-y);
            if(step%7==0){x=y=0;w=width;h=height;}
            for(unsigned yy=y;yy<y+h;++yy)for(unsigned xx=x;xx<x+w;++xx)
                p[(size_t(yy)*width+xx)*4+3]=(step%3)?255:uint8_t(rng()%256);
            certified=updated_opacity(certified,p.data(),width,height,x,y,w,h);
            bool actual=true;for(size_t i=3;i<p.size();i+=4)actual&=p[i]==255;
            assert(!certified||actual);
            if(size_t(width)*height>opacity_certificate_pixel_limit)assert(!certified);
            else if(!x&&!y&&w==width&&h==height)assert(certified==actual);
            ++checks;
        }
    }
    Vertex q[4]{};for(auto& v:q)v.a=1;
    assert(may_disable_blending(true,q,0,0));
    assert(!may_disable_blending(false,q,0,0));
    for(unsigned variant=1;variant<4;++variant)assert(!may_disable_blending(true,q,0,variant));
    assert(!may_disable_blending(true,q,1,0));
    for(unsigned i=0;i<4;++i)for(float a:{0.f,.5f,1.001f,NAN}){
        q[i].a=a;assert(!may_disable_blending(true,q,0,0));q[i].a=1;
    }
    std::printf("PASS %u opacity certification/update oracle checks and blend eligibility\n",checks);
}
