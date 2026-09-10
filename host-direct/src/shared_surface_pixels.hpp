#pragma once
#include "quad_trim.hpp"

namespace direct {
// Mapped CDRAM/uncached memory is expensive to read one alpha byte at a time.
// Probe the edges first (opaque backgrounds need no copy), then stage only the
// transparent margins in a small cached buffer. No second resident image.
template<class Copy>
AlphaBounds shared_alpha_bounds(const uint8_t* pixels,unsigned w,unsigned h,Copy copy){
    AlphaBounds b;b.known=true;b.left=w;b.top=h;
    alignas(16) uint8_t scratch[4096];
    constexpr unsigned chunk=sizeof(scratch)/4;
    auto include=[&](const uint8_t* row,unsigned y,unsigned x,unsigned end){
        while(x<end && !row[size_t(x)*4+3]){
            const unsigned n=std::min(chunk,end-x);
            copy(scratch,row+size_t(x)*4,size_t(n)*4);
            unsigned i=0;while(i<n&&!scratch[i*4+3])++i;
            x+=i;if(i<n)break;
        }
        if(x>=end)return;
        unsigned last=end-1;
        while(last>x && !row[size_t(last)*4+3]){
            const unsigned n=std::min(chunk,last-x);
            const unsigned start=last+1-n;
            copy(scratch,row+size_t(start)*4,size_t(n)*4);
            unsigned i=n;while(i&&!scratch[(i-1)*4+3])--i;
            last=start+i-1;if(i)break;
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
// backwards through cached scratch; every individual copy is non-overlapping.
// Only unpublished surfaces may be repacked this way.
template<class Copy>
void pack_shared_rows(uint8_t* pixels,unsigned w,unsigned h,unsigned stride,Copy copy){
    if(stride==w)return;
    alignas(16) uint8_t scratch[4096];
    for(unsigned y=h;y-->0;){
        auto* dst=pixels+size_t(y)*stride*4;
        const auto* src=pixels+size_t(y)*w*4;
        size_t remaining=size_t(w)*4;
        while(remaining){
            const size_t n=std::min(remaining,sizeof(scratch));remaining-=n;
            copy(scratch,src+remaining,n);copy(dst+remaining,scratch,n);
        }
        uint8_t edge[4];copy(edge,dst+size_t(w-1)*4,4);
        for(unsigned x=w;x<stride;++x)copy(dst+size_t(x)*4,edge,4);
    }
}
}
