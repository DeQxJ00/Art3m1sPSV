#pragma once
#include "bc3_layout.hpp"

namespace direct {
// Wire values shared with core::native_texture::Format. Containers contain
// row-major blocks except PVRTC1, whose PVR3 payload is already twiddled.
struct CompressedLayout {
    unsigned blockWidth=4,blockHeight=4,blockBytes=8;
    bool twiddled=false;
};
inline bool compressed_layout(unsigned format,CompressedLayout& l){
    if(format<1||format>14)return false;
    l={};
    if(format==2||format==3||format==6||format==7)l.blockBytes=16;
    if(format==8||format==9||format==12)l.blockWidth=8;
    l.twiddled=format>=8&&format<=11;
    return true;
}
inline unsigned compressed_extent(unsigned v){unsigned n=1;while(n<v)n*=2;return n;}
inline size_t compressed_bytes(unsigned format,unsigned w,unsigned h,bool storage){
    CompressedLayout l;
    if(!compressed_layout(format,l)||!w||!h||w>4096||h>4096)return 0;
    if(l.twiddled&&((w&(w-1))||(h&(h-1))||w<2*l.blockWidth||h<2*l.blockHeight))return 0;
    if(storage){w=compressed_extent(w);h=compressed_extent(h);}
    return size_t((w+l.blockWidth-1)/l.blockWidth)*((h+l.blockHeight-1)/l.blockHeight)*l.blockBytes;
}
inline bool compressed_swizzle(uint8_t* dst,size_t capacity,const uint8_t* src,size_t length,unsigned format,unsigned w,unsigned h){
    CompressedLayout l;
    const size_t input=compressed_bytes(format,w,h,false),output=compressed_bytes(format,w,h,true);
    if(!compressed_layout(format,l)||!dst||!src||!input||length!=input||capacity<output)return false;
    if(l.twiddled){std::memcpy(dst,src,input);return true;}
    const unsigned bw=std::max(1u,compressed_extent(w)/l.blockWidth),bh=std::max(1u,compressed_extent(h)/l.blockHeight);
    const unsigned sw=(w+l.blockWidth-1)/l.blockWidth,sh=(h+l.blockHeight-1)/l.blockHeight;
    std::memset(dst,0,output);
    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){
        auto* to=dst+bc3_block_index(x,y,bw,bh)*l.blockBytes;
        const auto* from=src+(size_t(y)*sw+x)*l.blockBytes;
        // ETC1 files store two big-endian words; GXM consumes little-endian
        // words. Reverse within each word, preserving the word order.
        if(format==14){for(unsigned k=0;k<8;++k)to[k]=from[k^3];}
        else std::memcpy(to,from,l.blockBytes);
    }
    return true;
}
}
