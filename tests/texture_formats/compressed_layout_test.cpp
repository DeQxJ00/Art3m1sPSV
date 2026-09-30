#include "../../host-direct/src/compressed_layout.hpp"
#include <cassert>
#include <vector>
int main(){
    using namespace direct;
    for(unsigned fmt=1;fmt<=14;++fmt){
        CompressedLayout l;assert(compressed_layout(fmt,l));
        const unsigned w=l.twiddled?32:17,h=l.twiddled?16:9;
        const auto n=compressed_bytes(fmt,w,h,false),cap=compressed_bytes(fmt,w,h,true);
        std::vector<uint8_t> source(n),output(cap+16,0xcd);
        for(size_t i=0;i<n;++i)source[i]=uint8_t(i/l.blockBytes+1);
        assert(compressed_swizzle(output.data(),cap,source.data(),n,fmt,w,h));
        for(size_t i=cap;i<output.size();++i)assert(output[i]==0xcd);
        if(l.twiddled){assert(std::equal(source.begin(),source.end(),output.begin()));}
        else {
            const auto sw=(w+l.blockWidth-1)/l.blockWidth,sh=(h+l.blockHeight-1)/l.blockHeight;
            const auto bw=compressed_extent(w)/l.blockWidth,bh=compressed_extent(h)/l.blockHeight;
            std::vector<bool> used(cap/l.blockBytes);
            for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){
                const auto at=bc3_block_index(x,y,bw,bh);assert(!used[at]);used[at]=true;
                for(unsigned k=0;k<l.blockBytes;++k)assert(output[at*l.blockBytes+k]==source[(y*sw+x)*l.blockBytes+k]);
            }
            for(size_t i=0;i<used.size();++i)if(!used[i])for(unsigned k=0;k<l.blockBytes;++k)assert(output[i*l.blockBytes+k]==0);
        }
        assert(!compressed_swizzle(output.data(),cap-1,source.data(),n,fmt,w,h));
        assert(!compressed_swizzle(output.data(),cap,source.data(),n-1,fmt,w,h));
        assert(!compressed_bytes(fmt,0,h,true));assert(!compressed_bytes(fmt,4097,h,true));
    }
    assert(!compressed_bytes(0,32,32,true));assert(!compressed_bytes(15,32,32,true));
    assert(!compressed_bytes(8,17,16,true));assert(!compressed_bytes(10,4,4,true));
    assert(compressed_bytes(3,17,9,true)==512);assert(compressed_bytes(1,17,9,true)==256);
    assert(compressed_bytes(12,17,9,true)==128);assert(compressed_bytes(3,1,1,true)==16);
    uint8_t etc[]={0,1,2,3,4,5,6,7},swapped[8]{};
    assert(compressed_swizzle(swapped,8,etc,8,14,4,4));
    for(unsigned k=0;k<8;++k)assert(swapped[k]==etc[k^3]);
}
