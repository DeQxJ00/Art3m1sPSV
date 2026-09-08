#pragma once
#include "quad_trim.hpp"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace direct {
// Alpha bounds describe nonzero pixels, not opacity. Only this full check may
// establish that every sampled texel is 255. Imported textures remain unknown.
inline bool pixels_are_opaque(const uint8_t* rgba,size_t pixels) {
    if(!rgba||!pixels)return false;
#if defined(__ARM_NEON)
    while(pixels>=16){
        const auto channels=vld4q_u8(rgba);
        const auto transparent=vreinterpretq_u64_u8(vmvnq_u8(channels.val[3]));
        if(vgetq_lane_u64(transparent,0)||vgetq_lane_u64(transparent,1))return false;
        rgba+=64;pixels-=16;
    }
#endif
    for(size_t i=0;i<pixels;++i)if(rgba[i*4+3]!=255)return false;
    return true;
}
inline bool updated_opacity(bool wasOpaque,const uint8_t* rgba,unsigned width,unsigned height,
                            unsigned x,unsigned y,unsigned w,unsigned h) {
    // Partial updates cannot prove that previously transparent pixels outside
    // the update became opaque. A complete replacement may establish opacity.
    if(!wasOpaque&&(x||y||w!=width||h!=height))return false;
    if(!w||!h)return wasOpaque;
    if(x==0&&w==width)return pixels_are_opaque(rgba+size_t(y)*width*4,size_t(w)*h);
    for(unsigned row=y;row<y+h;++row)
        if(!pixels_are_opaque(rgba+(size_t(row)*width+x)*4,w))return false;
    return true;
}
inline bool may_disable_blending(bool opaque,const Vertex* q,unsigned blend,unsigned variant){
    // Clip/rule programs can produce transparent output even for opaque input.
    if(!opaque||blend!=0||variant!=0)return false;
    for(unsigned i=0;i<4;++i)if(q[i].a!=1.0f)return false;
    return true;
}
}
