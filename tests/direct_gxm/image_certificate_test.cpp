#include "image_certificate.hpp"
#include "shared_surface_pixels.hpp"
#include "opaque_tiles.hpp"
#include <vector>
#include <random>
#include <cassert>
#include <cstdio>
using namespace direct;
int main(){
    std::mt19937 rng(915);unsigned checks=0;
    for(unsigned w:{1u,17u,63u,64u,65u,451u,960u,984u,1020u,1025u})
    for(unsigned h:{1u,9u,65u,541u})for(unsigned pattern=0;pattern<6;++pattern){
        std::vector<uint8_t> pixels(size_t(w)*h*4);
        AlphaBounds expected;expected.known=true;expected.left=w;expected.top=h;bool opaque=true;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
            auto* p=pixels.data()+(size_t(y)*w+x)*4;
            p[0]=uint8_t(rng());p[1]=uint8_t(rng());p[2]=uint8_t(rng());
            p[3]=pattern==0?0:pattern==1?255:pattern==2?(x==w/2&&y==h/2?1:0):
                 pattern==3?(x==y%w?254:0):pattern==4?(rng()%31==0?128:0):uint8_t(rng());
            opaque&=p[3]==255;if(p[3]){expected.left=std::min(expected.left,x);expected.top=std::min(expected.top,y);
                expected.right=std::max(expected.right,x+1);expected.bottom=std::max(expected.bottom,y+1);}
        }
        auto bounds=shared_alpha_bounds(pixels.data(),w,h,std::memcpy);
        OpaqueTiles tiles;tiles.build_reference(pixels.data(),w,h);
        std::vector<uint8_t> certificate(32+tiles.cells.size());
        write_image_certificate(certificate.data(),w,h,bounds,std::all_of(tiles.cells.begin(),tiles.cells.end(),[](uint8_t p){return p==1;}));
        std::memcpy(certificate.data()+32,tiles.cells.data(),tiles.cells.size());
        ImageCertificate c;assert(read_image_certificate(w,h,certificate.data(),certificate.size(),c));
        assert(c.opaque==opaque&&c.bounds.known&&c.bounds.left==expected.left&&c.bounds.top==expected.top&&c.bounds.right==expected.right&&c.bounds.bottom==expected.bottom);
        OpaqueTiles restored;assert(restored.assign_proof(w,h,c.tiles,c.count));assert(restored.cells==tiles.cells);
        assert(!read_image_certificate(w+1,h,certificate.data(),certificate.size(),c));
        assert(!read_image_certificate(w,h,certificate.data(),certificate.size()-1,c));
        assert(!read_image_certificate(w,h,tiles.cells.data(),tiles.cells.size(),c)); // Legacy proof must use fallback.
        for(unsigned i:{0u,28u,32u}){auto bad=certificate;bad[i]=2;assert(!read_image_certificate(w,h,bad.data(),bad.size(),c));}
        auto bad=certificate;bad[20]=0xff;bad[21]=0xff;assert(!read_image_certificate(w,h,bad.data(),bad.size(),c));
        ++checks;
    }
    std::printf("PASS %u certificate cases: exact alpha bounds, opacity, tile roundtrip, invalid/dimension/legacy fallback\n",checks);
}
