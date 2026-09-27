#include "../../host-direct/src/bc3_layout.hpp"
#include <cassert>
#include <vector>
int main(){
    using namespace direct;
    assert(!bc3_source_bytes(0,1)&&!bc3_storage_bytes(4097,1));
    assert(bc3_source_bytes(17,9)==240&&bc3_storage_bytes(17,9)==512);
    assert(bc3_source_bytes(3072,1536)==4718592&&bc3_storage_bytes(3072,1536)==8388608);
    for(unsigned bw=1;bw<=1024;bw*=2)for(unsigned bh=1;bh<=1024;bh*=2){
        std::vector<bool> seen(size_t(bw)*bh);
        for(unsigned y=0;y<bh;++y)for(unsigned x=0;x<bw;++x){
            const auto at=bc3_block_index(x,y,bw,bh);assert(at<seen.size()&&!seen[at]);seen[at]=true;
        }
    }
    std::vector<uint8_t> src(96),dst(128,255);
    for(unsigned i=0;i<6;++i)std::fill_n(src.data()+i*16,16,uint8_t(i+1));
    assert(bc3_swizzle(dst.data(),dst.size(),src.data(),src.size(),12,8));
    const unsigned order[]={1,4,2,5,3,6,0,0};
    for(unsigned i=0;i<8;++i)for(unsigned b=0;b<16;++b)assert(dst[i*16+b]==order[i]);
    assert(!bc3_swizzle(dst.data(),127,src.data(),src.size(),12,8));
    assert(!bc3_swizzle(dst.data(),128,src.data(),95,12,8));
}
