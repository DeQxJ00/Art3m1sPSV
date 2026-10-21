#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace direct {
struct ImageTextureSize {unsigned w,h;};
inline ImageTextureSize image_texture_size(unsigned w,unsigned h){
    const unsigned longest=std::max(w,h);
    if(longest<=4096)return {w,h};
    return {std::max(1u,unsigned(uint64_t(w)*4096/longest)),
            std::max(1u,unsigned(uint64_t(h)*4096/longest))};
}
// Pixel-centre bilinear sampling in premultiplied alpha, then back to straight
// alpha for BC encoding. Transparent RGB cannot bleed into visible edges.
inline void image_texture_sample(const uint8_t* src,unsigned w,unsigned h,
    ImageTextureSize size,unsigned x,unsigned y,uint8_t* out){
    x=std::min(x,size.w-1);y=std::min(y,size.h-1);
    if(w==size.w&&h==size.h){std::memcpy(out,src+(size_t(y)*w+x)*4,4);return;}
    const int64_t fx=std::max<int64_t>(0,int64_t((uint64_t(2*x+1)*w*256)/(2*size.w))-128);
    const int64_t fy=std::max<int64_t>(0,int64_t((uint64_t(2*y+1)*h*256)/(2*size.h))-128);
    const unsigned x0=std::min(unsigned(fx>>8),w-1),y0=std::min(unsigned(fy>>8),h-1);
    const unsigned ax=unsigned(fx)&255,ay=unsigned(fy)&255;
    const unsigned weights[4]={(256-ax)*(256-ay),ax*(256-ay),(256-ax)*ay,ax*ay};
    const uint8_t* p[4]={src+(size_t(y0)*w+x0)*4,src+(size_t(y0)*w+std::min(x0+1,w-1))*4,
        src+(size_t(std::min(y0+1,h-1))*w+x0)*4,src+(size_t(std::min(y0+1,h-1))*w+std::min(x0+1,w-1))*4};
    uint64_t alpha=0,rgb[3]={};
    for(unsigned i=0;i<4;++i){const uint64_t a=uint64_t(weights[i])*p[i][3];alpha+=a;for(unsigned c=0;c<3;++c)rgb[c]+=a*p[i][c];}
    out[3]=uint8_t((alpha+32768)/65536);
    for(unsigned c=0;c<3;++c)out[c]=alpha?uint8_t((rgb[c]+alpha/2)/alpha):0;
}
}
