#pragma once
#include "quad_trim.hpp"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace direct {
// Mapped CDRAM/uncached memory is expensive to read one alpha byte at a time.
// Probe edges first (opaque backgrounds need no copy). On Vita, read only the
// next 16 pixels into NEON and scan their extracted alpha in cached scratch.
// In particular do not copy a whole row for a sparse line near its edge.
template<class Copy>
AlphaBounds shared_alpha_bounds(const uint8_t* pixels,unsigned w,unsigned h,Copy copy){
    AlphaBounds b;b.known=true;b.left=w;b.top=h;
#if defined(__ARM_NEON)
    alignas(16) uint8_t scratch[16];
    auto alpha_block=[&](const uint8_t* p){
        const auto alpha=vld4q_u8(p).val[3];
        auto any=vmax_u8(vget_low_u8(alpha),vget_high_u8(alpha));
        any=vpmax_u8(any,any);any=vpmax_u8(any,any);any=vpmax_u8(any,any);
        if(!vget_lane_u8(any,0))return false;
        vst1q_u8(scratch,alpha);return true;
    };
#else
    alignas(16) uint8_t scratch[4096];
    constexpr unsigned chunk=sizeof(scratch)/4;
#endif
    auto include=[&](const uint8_t* row,unsigned y,unsigned x,unsigned end){
        while(x<end && !row[size_t(x)*4+3]){
#if defined(__ARM_NEON)
            if(end-x<16){++x;continue;}
            if(!alpha_block(row+size_t(x)*4)){x+=16;continue;}
            unsigned i=0;while(!scratch[i])++i;x+=i;break;
#else
            const unsigned n=std::min(chunk,end-x);
            copy(scratch,row+size_t(x)*4,size_t(n)*4);
            unsigned i=0;while(i<n&&!scratch[i*4+3])++i;
            x+=i;if(i<n)break;
#endif
        }
        if(x>=end)return;
        unsigned last=end-1;
        while(last>x && !row[size_t(last)*4+3]){
#if defined(__ARM_NEON)
            if(last-x<16){--last;continue;}
            if(!alpha_block(row+size_t(last+1-16)*4)){last-=16;continue;}
            unsigned i=16;while(!scratch[i-1])--i;last=last+1-16+i-1;break;
#else
            const unsigned n=std::min(chunk,last-x);
            const unsigned start=last+1-n;
            copy(scratch,row+size_t(start)*4,size_t(n)*4);
            unsigned i=n;while(i&&!scratch[(i-1)*4+3])--i;
            last=start+i-1;if(i)break;
#endif
        }
        b.left=std::min(b.left,x);b.right=std::max(b.right,last+1);
        b.top=std::min(b.top,y);b.bottom=std::max(b.bottom,y+1);
    };
    for(unsigned y=0;y<h;++y){
        const auto* row=pixels+size_t(y)*w*4;
        if(y>=b.top&&y<b.bottom){
            include(row,y,0,b.left);include(row,y,b.right,w);
        }else include(row,y,0,w);
    }
    return b;
}

// The source and destination overlap, so a direct memcpy is invalid. Copy
// backwards through NEON registers (or cached scratch on other platforms).
// Only unpublished surfaces may be repacked this way.
template<class Copy>
void pack_shared_rows(uint8_t* pixels,unsigned w,unsigned h,unsigned stride,Copy copy){
    if(stride==w)return;
#if !defined(__ARM_NEON)
    alignas(16) uint8_t scratch[4096];
#endif
    for(unsigned y=h;y-->0;){
        auto* dst=pixels+size_t(y)*stride*4;
        const auto* src=pixels+size_t(y)*w*4;
        size_t remaining=size_t(w)*4;
#if defined(__ARM_NEON)
        while(remaining>=64){
            remaining-=64;
            // Load the entire block before any overlapping store.
            const auto a=vld1q_u8(src+remaining),b=vld1q_u8(src+remaining+16);
            const auto c=vld1q_u8(src+remaining+32),d=vld1q_u8(src+remaining+48);
            vst1q_u8(dst+remaining,a);vst1q_u8(dst+remaining+16,b);
            vst1q_u8(dst+remaining+32,c);vst1q_u8(dst+remaining+48,d);
        }
        while(remaining>=16){remaining-=16;const auto a=vld1q_u8(src+remaining);vst1q_u8(dst+remaining,a);}
        while(remaining){remaining-=4;uint8_t pixel[4];copy(pixel,src+remaining,4);copy(dst+remaining,pixel,4);}
#else
        while(remaining){
            const size_t n=std::min(remaining,sizeof(scratch));remaining-=n;
            copy(scratch,src+remaining,n);copy(dst+remaining,scratch,n);
        }
#endif
        uint8_t edge[4];copy(edge,dst+size_t(w-1)*4,4);
        for(unsigned x=w;x<stride;++x)copy(dst+size_t(x)*4,edge,4);
    }
}
}
