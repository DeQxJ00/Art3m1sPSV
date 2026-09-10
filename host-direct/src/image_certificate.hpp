#pragma once
#include "quad_trim.hpp"
#include <cstring>

namespace direct {
// Versioned CPU-generated certificate, owned with one immutable decoded source.
// Little-endian words: magic, width, height, left, top, right, bottom, opaque.
// The unchanged 64x64 alpha tile flags follow the 32-byte header.
constexpr size_t imageCertificateHeader=32;
constexpr uint32_t imageCertificateMagic=0x31504641;
inline uint32_t certificate_word(const uint8_t* p,unsigned i){
    p+=i*4;return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}
inline void write_image_certificate(uint8_t* p,unsigned w,unsigned h,const AlphaBounds& b,bool opaque){
    const uint32_t words[]={imageCertificateMagic,w,h,b.left,b.top,b.right,b.bottom,uint32_t(opaque)};
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)p[i*4+j]=uint8_t(words[i]>>(j*8));
}
struct ImageCertificate { AlphaBounds bounds; bool opaque=false; const uint8_t* tiles=nullptr; size_t count=0; };
inline bool read_image_certificate(unsigned w,unsigned h,const uint8_t* data,size_t size,ImageCertificate& out){
    const size_t count=((size_t(w)+63)/64)*((size_t(h)+63)/64);
    if(!w||!h||!data||size!=imageCertificateHeader+count||certificate_word(data,0)!=imageCertificateMagic||
       certificate_word(data,1)!=w||certificate_word(data,2)!=h)return false;
    AlphaBounds b;b.known=true;b.left=certificate_word(data,3);b.top=certificate_word(data,4);
    b.right=certificate_word(data,5);b.bottom=certificate_word(data,6);
    const auto opaque=certificate_word(data,7);
    const bool empty=b.left==w&&b.top==h&&!b.right&&!b.bottom;
    if(opaque>1||(!empty&&!(b.left<b.right&&b.top<b.bottom&&b.right<=w&&b.bottom<=h)))return false;
    bool all=true;const auto* tiles=data+imageCertificateHeader;
    for(size_t i=0;i<count;++i){if(tiles[i]>1)return false;all&=tiles[i]!=0;}
    if(all!=(opaque!=0)||(opaque&&(b.left||b.top||b.right!=w||b.bottom!=h)))return false;
    out.bounds=b;out.opaque=opaque!=0;out.tiles=tiles;out.count=count;return true;
}
}
