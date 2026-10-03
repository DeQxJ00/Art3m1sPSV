#include "../../host-direct/src/sparse_surface_pixels.hpp"
#include <cassert>
#include <cstring>
#include <vector>
#include <random>
int main(){
    std::mt19937 rng(7321);
    for(size_t w:{1,7,8,9,397,513,960})for(size_t h:{1,3,257}){
        size_t stride=(w+7)&~size_t(7),bytes=w*h*4;
        std::vector<uint8_t> gpu(stride*h*4+32,0x99),reference(bytes);
        for(size_t at=0;at<bytes;){size_t n=std::min(bytes-at,size_t(4*(1+rng()%32768)));bool zero=(rng()%3)!=0;
            std::vector<uint8_t> literal(n);if(!zero){for(auto& b:literal)b=uint8_t(rng());std::copy(literal.begin(),literal.end(),reference.begin()+at);}
            assert(direct::sparse_surface_write(gpu.data(),w,h,stride,at,zero?nullptr:literal.data(),n,std::memcpy,std::memset));at+=n;
        }
        for(size_t y=0;y<h;++y)for(size_t x=0;x<stride;++x){auto sx=std::min(x,w-1);assert(std::memcmp(gpu.data()+(y*stride+x)*4,reference.data()+(y*w+sx)*4,4)==0);}
        for(size_t i=stride*h*4;i<gpu.size();++i)assert(gpu[i]==0x99);
        assert(!direct::sparse_surface_write(gpu.data(),w,h,stride,bytes,nullptr,4,std::memcpy,std::memset));
        assert(!direct::sparse_surface_write(gpu.data(),w,h,stride,1,nullptr,4,std::memcpy,std::memset));
    }
}
