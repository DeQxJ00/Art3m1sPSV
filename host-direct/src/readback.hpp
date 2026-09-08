#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace direct {
// CPU copy only. Caller must supply a completed 960x544 RGBA framebuffer,
// whose row stride is 1024 pixels, and a w*h*4 byte output allocation.
inline void copy_completed_frame(const uint8_t* src,unsigned w,unsigned h,uint8_t* out){
    if(w==960){for(unsigned y=0;y<h;y++)std::memcpy(out+size_t(y)*w*4,src+(size_t(y)*544/h)*1024*4,w*4);return;}
    uint8_t row[960*4];unsigned cachedY=~0u;
    for(unsigned y=0;y<h;y++){const unsigned sourceY=size_t(y)*544/h;
        if(sourceY!=cachedY){std::memcpy(row,src+size_t(sourceY)*1024*4,sizeof(row));cachedY=sourceY;}
        for(unsigned x=0;x<w;x++)std::memcpy(out+(size_t(y)*w+x)*4,row+(size_t(x)*960/w)*4,4);
    }
}
}
