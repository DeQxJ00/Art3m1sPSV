#include "readback.hpp"
#include <vector>
#include <cstdio>
int main(){
    std::vector<uint8_t> src(1024*544*4);
    for(unsigned y=0;y<544;y++)for(unsigned x=0;x<1024;x++)for(unsigned c=0;c<4;c++)
        src[(y*1024+x)*4+c]=uint8_t(x*17+y*31+c*47+(x*y)%251);
    const unsigned sizes[][2]={{960,544},{960,540},{320,180},{1200,680},{987,541},{1,1},{7,883}};
    for(auto& size:sizes){unsigned w=size[0],h=size[1];std::vector<uint8_t> out(size_t(w)*h*4);
        direct::copy_completed_frame(src.data(),w,h,out.data());
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++)
            if(out[(size_t(y)*w+x)*4+c]!=src[(size_t(y)*544/h*1024+size_t(x)*960/w)*4+c])return 1;
        std::printf("PASS production readback %ux%u exact RGBA and stride\n",w,h);
    }
}
