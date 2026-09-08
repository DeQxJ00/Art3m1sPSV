#pragma once
#include <cstddef>
#include <cstdint>

namespace direct {
// All visible texels are supplied by the caller. Only stride padding needs
// clearing; clearing the whole uncached destination first doubles its writes.
template<class Copy,class Clear>
void initialize_texture_pixels(uint8_t* dst,unsigned stride,const uint8_t* src,
                               unsigned width,unsigned height,Copy copy,Clear clear){
    if(stride==width){copy(dst,src,size_t(width)*height*4);return;}
    for(unsigned y=0;y<height;++y){
        auto* row=dst+size_t(y)*stride*4;
        copy(row,src+size_t(y)*width*4,size_t(width)*4);
        clear(row+size_t(width)*4,0,size_t(stride-width)*4);
    }
}
template<class Copy>
void update_texture_pixels(uint8_t* dst,unsigned stride,const uint8_t* src,unsigned width,
                           unsigned x,unsigned y,unsigned w,unsigned h,Copy copy){
    if(x==0&&w==width&&stride==width){
        copy(dst+size_t(y)*stride*4,src+size_t(y)*width*4,size_t(w)*h*4);return;
    }
    for(unsigned row=y;row<y+h;++row)
        copy(dst+(size_t(row)*stride+x)*4,src+(size_t(row)*width+x)*4,size_t(w)*4);
}
}
