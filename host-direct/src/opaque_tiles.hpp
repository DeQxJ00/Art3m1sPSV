#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "quad_trim.hpp"
namespace direct {
// Conservative CPU-source proof. Unknown or partially transparent tiles never qualify.
struct OpaqueTiles {
    static constexpr unsigned tile=64;
    unsigned width=0,height=0,columns=0;
    std::vector<uint8_t> cells;
    void clear(){cells.clear();}
    void build(const uint8_t* rgba,unsigned w,unsigned h){
        width=w;height=h;columns=(w+tile-1)/tile;
        cells.assign(columns*((h+tile-1)/tile),0);
        if(!rgba||!w||!h)return;
        for(unsigned ty=0;ty<h;ty+=tile)for(unsigned tx=0;tx<w;tx+=tile){
            bool opaque=true;
            for(unsigned y=ty;y<std::min(ty+tile,h)&&opaque;++y)
                for(unsigned x=tx;x<std::min(tx+tile,w);++x)
                    if(rgba[(size_t(y)*w+x)*4+3]!=255){opaque=false;break;}
            cells[(ty/tile)*columns+tx/tile]=opaque;
        }
    }
    bool covers(float u0,float v0,float u1,float v1)const{
        if(cells.empty()||!width||!height||!std::isfinite(u0)||!std::isfinite(v0)
            ||!std::isfinite(u1)||!std::isfinite(v1))return false;
        if(u0>u1)std::swap(u0,u1);if(v0>v1)std::swap(v0,v1);
        if(u0<0||v0<0||u1>1||v1>1)return false;
        // Include a full texel guard on both sides for bilinear filtering/rounding.
        unsigned x0=unsigned(std::max(0.f,std::floor(u0*width)-1));
        unsigned y0=unsigned(std::max(0.f,std::floor(v0*height)-1));
        unsigned x1=unsigned(std::min(float(width-1),std::ceil(u1*width)+1));
        unsigned y1=unsigned(std::min(float(height-1),std::ceil(v1*height)+1));
        for(unsigned y=y0/tile;y<=y1/tile;++y)for(unsigned x=x0/tile;x<=x1/tile;++x)
            if(!cells[y*columns+x])return false;
        return true;
    }
    bool covers_visible_quad(const Vertex* q)const{
        if(cells.empty()||!q)return false;
        for(unsigned i=0;i<4;++i)if(!std::isfinite(q[i].x)||!std::isfinite(q[i].y))return false;
        if(q[0].x!=q[2].x||q[1].x!=q[3].x||q[0].y!=q[1].y||q[2].y!=q[3].y
            ||q[0].u!=q[2].u||q[1].u!=q[3].u||q[0].v!=q[1].v||q[2].v!=q[3].v)return false;
        float dx=q[1].x-q[0].x,dy=q[2].y-q[0].y;if(dx==0||dy==0)return false;
        float x0=std::max(0.f,std::min(q[0].x,q[1].x)),x1=std::min(960.f,std::max(q[0].x,q[1].x));
        float y0=std::max(0.f,std::min(q[0].y,q[2].y)),y1=std::min(544.f,std::max(q[0].y,q[2].y));
        if(x0>=x1||y0>=y1)return false;
        auto u=[&](float x){return q[0].u+(x-q[0].x)/dx*(q[1].u-q[0].u);};
        auto v=[&](float y){return q[0].v+(y-q[0].y)/dy*(q[2].v-q[0].v);};
        return covers(u(x0),v(y0),u(x1),v(y1));
    }
};
}
