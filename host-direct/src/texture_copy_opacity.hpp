#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace direct {
// Experimental helper, not wired into uploads yet. Source and destination must
// not overlap. Inspect bytes already loaded for the copy, never read CDRAM back.
template<class Copy>
bool copy_and_certify_rgba(uint8_t* dst,const uint8_t* src,size_t pixels,Copy copy){
    if(!pixels)return false;
    while(pixels){
        // Transparent assets usually fail immediately; retain optimized memcpy
        // for everything remaining once an opaque certificate is impossible.
        if(src[3]!=255){copy(dst,src,pixels*4);return false;}
#if defined(__ARM_NEON)
        if(pixels>=16){
            const size_t blocks=std::min<size_t>(pixels/16,16);
            auto bits=vdupq_n_u8(255);
            for(size_t i=0;i<blocks;++i){
                const auto a=vld1q_u8(src),b=vld1q_u8(src+16);
                const auto c=vld1q_u8(src+32),d=vld1q_u8(src+48);
                vst1q_u8(dst,a);vst1q_u8(dst+16,b);
                vst1q_u8(dst+32,c);vst1q_u8(dst+48,d);
                bits=vandq_u8(bits,vandq_u8(vandq_u8(a,b),vandq_u8(c,d)));
                src+=64;dst+=64;
            }
            pixels-=blocks*16;
            const auto words=vreinterpretq_u32_u8(bits);
            auto pair=vand_u32(vget_low_u32(words),vget_high_u32(words));
            pair=vand_u32(pair,vrev64_u32(pair));
            if((vget_lane_u32(pair,0)&0xff000000u)!=0xff000000u){
                if(pixels)copy(dst,src,pixels*4);
                return false;
            }
            continue;
        }
#endif
        for(unsigned c=0;c<4;++c)dst[c]=src[c];
        src+=4;dst+=4;--pixels;
    }
    return true;
}

template<class Copy,class Clear>
bool initialize_pixels_with_opacity(uint8_t* dst,unsigned stride,const uint8_t* src,
                                   unsigned width,unsigned height,Copy copy,Clear clear){
    if(!width||!height)return false;
    if(stride==width)return copy_and_certify_rgba(dst,src,size_t(width)*height,copy);
    bool opaque=true;
    for(unsigned y=0;y<height;++y){
        auto* row=dst+size_t(y)*stride*4;const auto* input=src+size_t(y)*width*4;
        if(opaque)opaque=copy_and_certify_rgba(row,input,width,copy);
        else copy(row,input,size_t(width)*4);
        clear(row+size_t(width)*4,0,size_t(stride-width)*4);
    }
    return opaque;
}
}
