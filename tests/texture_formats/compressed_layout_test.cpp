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
    // Frozen per-block reference, nonuniform bytes, skinny/NPOT/full-size
    // textures. Include the main-game 3072x1536 BC3 atlas and zero padding.
    for(unsigned fmt=1;fmt<=14;++fmt){CompressedLayout l;assert(compressed_layout(fmt,l));if(l.twiddled)continue;
      const unsigned sizes[][2]={{1,1},{1,4096},{4096,1},{63,17},{17,63},{3072,1536},{1536,3072},{4096,4096}};
      for(auto& size:sizes){unsigned w=size[0],h=size[1];auto n=compressed_bytes(fmt,w,h,false),cap=compressed_bytes(fmt,w,h,true);
        std::vector<uint8_t> source(n),expected(cap),actual(cap+16,0xcd);
        for(size_t i=0;i<n;++i)source[i]=uint8_t((i*37)^(i>>8));
        unsigned sw=(w+l.blockWidth-1)/l.blockWidth,sh=(h+l.blockHeight-1)/l.blockHeight;
        unsigned bw=std::max(1u,compressed_extent(w)/l.blockWidth),bh=std::max(1u,compressed_extent(h)/l.blockHeight);
        for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x){size_t dest=bc3_block_index(x,y,bw,bh)*l.blockBytes,src=(size_t(y)*sw+x)*l.blockBytes;
          for(unsigned k=0;k<l.blockBytes;++k)expected[dest+k]=source[src+(fmt==14?(k^3):k)];}
        assert(compressed_swizzle(actual.data(),cap,source.data(),n,fmt,w,h));
        assert(std::equal(expected.begin(),expected.end(),actual.begin()));for(size_t i=cap;i<actual.size();++i)assert(actual[i]==0xcd);
      }
    }
    uint8_t etc[]={0,1,2,3,4,5,6,7},swapped[8]{};
    assert(compressed_swizzle(swapped,8,etc,8,14,4,4));
    for(unsigned k=0;k<8;++k)assert(swapped[k]==etc[k^3]);
}
