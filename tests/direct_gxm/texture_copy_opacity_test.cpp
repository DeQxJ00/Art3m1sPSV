#include "texture_copy_opacity.hpp"
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
int main(){
    unsigned cases=0;
    for(unsigned width:{1u,3u,15u,16u,17u,63u,64u,65u,255u,256u,257u,960u,1920u})
    for(unsigned height:{1u,3u,9u})for(unsigned pad:{0u,3u})for(unsigned offset:{0u,1u,7u}){
        size_t pixels=size_t(width)*height;unsigned stride=width+pad;
        for(size_t bad:{pixels,size_t(0),pixels/2,pixels-1}){
            std::vector<uint8_t> source(pixels*4+offset+32,0xcd);
            for(size_t i=0;i<pixels;++i){source[offset+i*4]=uint8_t(i*31);source[offset+i*4+1]=uint8_t(i*73);source[offset+i*4+2]=0;source[offset+i*4+3]=255;}
            if(bad<pixels)source[offset+bad*4+3]=254;
            auto sourceCopy=source;
            const size_t bytes=size_t(stride)*height*4;
            std::vector<uint8_t> target(bytes+offset+32,0xa5),expected=target;
            for(unsigned y=0;y<height;++y){
                std::memcpy(expected.data()+offset+size_t(y)*stride*4,source.data()+offset+size_t(y)*width*4,size_t(width)*4);
                std::memset(expected.data()+offset+(size_t(y)*stride+width)*4,0,size_t(pad)*4);
            }
            bool opaque=direct::initialize_pixels_with_opacity(target.data()+offset,stride,source.data()+offset,width,height,std::memcpy,std::memset);
            assert(opaque==(bad==pixels));assert(target==expected);assert(source==sourceCopy);++cases;
        }
    }
    // Exhaustively move a single bad alpha through block/reduction boundaries.
    for(size_t bad=0;bad<513;++bad){
        std::vector<uint8_t> src(513*4,255),dst(src.size());src[bad*4+3]=0;
        assert(!direct::copy_and_certify_rgba(dst.data(),src.data(),513,std::memcpy));assert(src==dst);++cases;
    }
    std::printf("PASS %u copy/certificate cases: byte-exact pixels, padding, guards, unaligned buffers\n",cases);
}
