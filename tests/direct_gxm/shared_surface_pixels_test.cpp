#include "shared_surface_pixels.hpp"
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <random>
using namespace direct;
int main(){
    std::mt19937 rng(9911);unsigned checks=0;
    for(unsigned w:{1u,7u,17u,255u,451u,594u,960u,984u,1020u,1024u,1025u,2049u,4096u})
    for(unsigned h:{1u,9u,61u})for(unsigned pattern=0;pattern<8;++pattern){
        const unsigned stride=(w+7)&~7u;
        std::vector<uint8_t> p(size_t(w)*h*4);
        AlphaBounds expected;expected.known=true;expected.left=w;expected.top=h;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            auto* pixel=&p[(size_t(y)*w+x)*4];
            for(unsigned c=0;c<3;++c)pixel[c]=uint8_t(rng());
            bool visible=pattern==1||(pattern==2&&x==w/2&&y==h/2)||
                (pattern==3&&x>w/4&&x<w*3/4&&y>h/4&&y<h*3/4)||
                (pattern==4&&(x==0||x==w-1))||(pattern==5&&(y==0||y==h-1))||
                (pattern==6&&rng()%101==0)||(pattern==7&&rng()%2);
            pixel[3]=visible?uint8_t(1+rng()%255):0;
            if(visible){expected.left=std::min(expected.left,x);expected.top=std::min(expected.top,y);
                expected.right=std::max(expected.right,x+1);expected.bottom=std::max(expected.bottom,y+1);}
        }
        auto b=shared_alpha_bounds(p.data(),w,h,std::memcpy);
        assert(b.known&&b.left==expected.left&&b.top==expected.top&&b.right==expected.right&&b.bottom==expected.bottom);
        std::vector<uint8_t> out(size_t(stride)*h*4+128,0xa5);
        auto* dst=out.data()+64;std::memcpy(dst,p.data(),p.size());
        pack_shared_rows(dst,w,h,stride,std::memcpy);
        for(unsigned y=0;y<h;++y){
            assert(!std::memcmp(dst+size_t(y)*stride*4,p.data()+size_t(y)*w*4,w*4));
            for(unsigned x=w;x<stride;++x)assert(!std::memcmp(dst+(size_t(y)*stride+x)*4,p.data()+(size_t(y)*w+w-1)*4,4));
        }
        for(unsigned i=0;i<64;++i)assert(out[i]==0xa5&&out[out.size()-1-i]==0xa5);
        ++checks;
    }
    std::printf("PASS %u shared bounds/row packing cases against brute force and packed source; allocation guards intact\n",checks);
}
