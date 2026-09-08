#include "texture_pixels.hpp"
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>

int main(){
    unsigned checks=0;
    for(unsigned width=1;width<=73;++width)for(unsigned height:{1u,7u,23u}){
        const unsigned stride=(width+7)&~7u;
        std::vector<uint8_t> source(size_t(width)*height*4),target(size_t(stride)*height*4+64,0xa5);
        for(size_t i=0;i<source.size();++i)source[i]=uint8_t(i*73+17);
        direct::initialize_texture_pixels(target.data(),stride,source.data(),width,height,std::memcpy,std::memset);
        for(unsigned y=0;y<height;++y){
            assert(!std::memcmp(target.data()+size_t(y)*stride*4,source.data()+size_t(y)*width*4,width*4));
            for(unsigned x=width*4;x<stride*4;++x)assert(target[size_t(y)*stride*4+x]==0);
        }
        for(unsigned full=0;full<2;++full){
            unsigned x=full?0:width/3,y=full?0:height/3;
            unsigned w=full?width:width-x,h=full?height:height-y;
            auto expected=target;
            for(auto& byte:source)byte^=0x6d;
            for(unsigned yy=y;yy<y+h;++yy)for(unsigned xx=x;xx<x+w;++xx)for(unsigned c=0;c<4;++c)
                expected[(size_t(yy)*stride+xx)*4+c]=source[(size_t(yy)*width+xx)*4+c];
            direct::update_texture_pixels(target.data(),stride,source.data(),width,x,y,w,h,std::memcpy);
            assert(target==expected);
            ++checks;
        }
        for(size_t i=size_t(stride)*height*4;i<target.size();++i)assert(target[i]==0xa5);
    }
    std::printf("PASS %u full/partial texture updates; odd strides, padding and allocation guards\n",checks);
}
