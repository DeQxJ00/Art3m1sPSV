#pragma once
#include "opaque_tiles.hpp"

namespace direct {
// Candidate only: preserve the exact 64x64 proof while visiting source rows in
// memory order. No GPU memory is read and no approximate alpha test is used.
inline void build_opaque_tiles_row_major(OpaqueTiles& out,const uint8_t* rgba,unsigned w,unsigned h){
    out.width=w;out.height=h;out.columns=(w+63)/64;
    out.cells.assign(size_t(out.columns)*((h+63)/64),0);
    if(!rgba||!w||!h)return;
#if defined(__ARM_NEON)
    std::vector<uint32_t> accum(size_t(out.columns)*4);
#endif
    for(unsigned ty=0;ty<h;ty+=64){
        auto* cells=out.cells.data()+size_t(ty/64)*out.columns;
        std::fill(cells,cells+out.columns,1);
#if defined(__ARM_NEON)
        // Four lanes per tile. Reduction stays in NEON throughout the strip;
        // moving lane results to ARM for every row stalls Cortex-A9 pipelines.
        std::fill(accum.begin(),accum.end(),0xffffffffu);
#endif
        for(unsigned y=ty;y<std::min(ty+64,h);++y){
            const auto* row=rgba+size_t(y)*w*4;
            for(unsigned tx=0;tx<w;tx+=64){
                const unsigned cell=tx/64,n=std::min(64u,w-tx);
                if(!cells[cell])continue;
                const auto* p=row+size_t(tx)*4;
                if(p[3]!=255){cells[cell]=0;continue;}
                unsigned x=0;
#if defined(__ARM_NEON)
                auto bits=vld1q_u32(accum.data()+size_t(cell)*4);
                for(;x+16<=n;x+=16){
                    auto a=vld1q_u8(p+x*4),b=vld1q_u8(p+x*4+16);
                    auto c=vld1q_u8(p+x*4+32),d=vld1q_u8(p+x*4+48);
                    bits=vandq_u32(bits,vreinterpretq_u32_u8(vandq_u8(vandq_u8(a,b),vandq_u8(c,d))));
                }
                for(;x+4<=n;x+=4)bits=vandq_u32(bits,vreinterpretq_u32_u8(vld1q_u8(p+x*4)));
                vst1q_u32(accum.data()+size_t(cell)*4,bits);
#endif
                for(;x<n;++x)if(p[x*4+3]!=255){cells[cell]=0;break;}
            }
        }
#if defined(__ARM_NEON)
        for(unsigned c=0;c<out.columns;++c){
            const auto* bits=accum.data()+size_t(c)*4;
            cells[c]=cells[c]&&((bits[0]&bits[1]&bits[2]&bits[3]&0xff000000u)==0xff000000u);
        }
#endif
    }
}
}
