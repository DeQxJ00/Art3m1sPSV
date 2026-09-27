#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace direct {
inline unsigned bc3_extent(unsigned v){
    unsigned n=4;while(n<v)n*=2;return n;
}
inline size_t bc3_source_bytes(unsigned w,unsigned h){
    return w&&h&&w<=4096&&h<=4096?size_t((w+3)/4)*((h+3)/4)*16:0;
}
inline size_t bc3_storage_bytes(unsigned w,unsigned h){
    return bc3_source_bytes(w,h)?size_t(bc3_extent(w))*bc3_extent(h):0;
}
// GXM swizzles 128-bit BC3 blocks, with Y in the low Morton bit. Once
// the shorter power-of-two dimension is exhausted, the longer one continues.
inline size_t bc3_block_index(unsigned x,unsigned y,unsigned bw,unsigned bh){
    size_t index=0;unsigned shift=0;
    for(unsigned bit=1;bit<bw||bit<bh;bit*=2){
        if(bit<bh){index|=size_t((y&bit)!=0)<<shift;++shift;}
        if(bit<bw){index|=size_t((x&bit)!=0)<<shift;++shift;}
    }
    return index;
}
inline bool bc3_swizzle(uint8_t* dst,size_t capacity,const uint8_t* src,size_t length,unsigned w,unsigned h){
    const size_t input=bc3_source_bytes(w,h),output=bc3_storage_bytes(w,h);
    if(!dst||!src||!input||length!=input||capacity<output)return false;
    std::memset(dst,0,output);
    const unsigned bw=bc3_extent(w)/4,bh=bc3_extent(h)/4,sw=(w+3)/4,sh=(h+3)/4;
    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x)
        std::memcpy(dst+bc3_block_index(x,y,bw,bh)*16,src+(size_t(y)*sw+x)*16,16);
    return true;
}
}
