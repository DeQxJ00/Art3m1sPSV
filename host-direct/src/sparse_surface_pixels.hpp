#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace direct {
// Logical RGBA spans -> private strided GPU storage. Never read GPU memory.
// NULL source is a zero span; border padding repeats the last logical pixel.
template<class Copy,class Fill>
bool sparse_surface_write(uint8_t* pixels,size_t width,size_t height,size_t stridePixels,
    size_t offset,const uint8_t* source,size_t len,Copy copy,Fill fill){
    const auto max=std::numeric_limits<size_t>::max();
    if(!pixels||!width||!height||stridePixels<width||stridePixels>max/4||height>max/(stridePixels*4))return false;
    const size_t row=width*4,stride=stridePixels*4,total=row*height;
    if(offset%4||len%4||offset>total||len>total-offset)return false;
    if(row==stride){if(source)copy(pixels+offset,source,len);else fill(pixels+offset,0,len);return true;}
    while(len){
        const size_t y=offset/row,x=offset%row;
        if(!source&&x==0&&len>=row){const size_t rows=len/row;fill(pixels+y*stride,0,rows*stride);offset+=rows*row;len-=rows*row;continue;}
        const size_t take=std::min(len,row-x);auto* dst=pixels+y*stride+x;
        if(source)copy(dst,source,take);else fill(dst,0,take);
        if(x+take==row&&stride>row){for(size_t pad=row;pad<stride;pad+=4){auto* end=pixels+y*stride+pad;if(source)copy(end,source+take-4,4);else fill(end,0,4);}}
        offset+=take;len-=take;if(source)source+=take;
    }
    return true;
}
}
