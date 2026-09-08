#pragma once
#include "quad_trim.hpp"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace direct {
constexpr size_t opacity_certificate_pixel_limit=1024;
// Alpha bounds describe nonzero pixels, not opacity. Only this full check may
// establish that every sampled texel is 255. Imported textures remain unknown.
inline bool pixels_are_opaque(const uint8_t* rgba,size_t pixels) {
    if(!rgba||!pixels)return false;
    if(rgba[3]!=255)return false;
#if defined(__ARM_NEON)
    while(pixels>=64){
        // Keep the reduction inside NEON. Moving lane results to ARM every
        // 16 pixels serializes the two pipelines on Cortex-A9 (optC measured
        // ~14 ms per 960x540 capture). Contiguous loads also avoid deinterleave.
        auto bits=vdupq_n_u8(255);
        const size_t blocks=std::min<size_t>(pixels/64,16);
        for(size_t block=0;block<blocks;++block){
            const auto a=vandq_u8(vld1q_u8(rgba),vld1q_u8(rgba+16));
            const auto b=vandq_u8(vld1q_u8(rgba+32),vld1q_u8(rgba+48));
            const auto c=vandq_u8(vld1q_u8(rgba+64),vld1q_u8(rgba+80));
            const auto d=vandq_u8(vld1q_u8(rgba+96),vld1q_u8(rgba+112));
            const auto e=vandq_u8(vld1q_u8(rgba+128),vld1q_u8(rgba+144));
            const auto f=vandq_u8(vld1q_u8(rgba+160),vld1q_u8(rgba+176));
            const auto g=vandq_u8(vld1q_u8(rgba+192),vld1q_u8(rgba+208));
            const auto h=vandq_u8(vld1q_u8(rgba+224),vld1q_u8(rgba+240));
            bits=vandq_u8(bits,vandq_u8(vandq_u8(vandq_u8(a,b),vandq_u8(c,d)),
                                       vandq_u8(vandq_u8(e,f),vandq_u8(g,h))));
            rgba+=256;
        }
        // RGBA alpha occupies byte 3 of every word; other bytes are ignored.
        auto words=vreinterpretq_u32_u8(bits);
        auto pair=vand_u32(vget_low_u32(words),vget_high_u32(words));
        pair=vand_u32(pair,vrev64_u32(pair));
        if((vget_lane_u32(pair,0)&0xff000000u)!=0xff000000u)return false;
        pixels-=blocks*64;
    }
#endif
    for(size_t i=0;i<pixels;++i)if(rgba[i*4+3]!=255)return false;
    return true;
}
inline bool certify_texture_opacity(const uint8_t* rgba,size_t pixels){
    // Hardware optC/optD: scanning a 960x540 capture costs 12-15 ms and its
    // later fade usually cannot use opaque blending. Cap optional work before
    // touching memory. Large textures remain conservatively unknown.
    return pixels<=opacity_certificate_pixel_limit&&pixels_are_opaque(rgba,pixels);
}
inline bool updated_opacity(bool wasOpaque,const uint8_t* rgba,unsigned width,unsigned height,
                            unsigned x,unsigned y,unsigned w,unsigned h) {
    if(size_t(width)*height>opacity_certificate_pixel_limit)return false;
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
