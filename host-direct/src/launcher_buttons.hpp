#pragma once
#include "gpu.hpp"
#include <array>
#include <cmath>
#include <algorithm>

namespace direct {
// Small antialiased button artwork, independent of the selected UI font.
inline Texture* make_launcher_buttons(){
    std::array<uint8_t,120*24*4> rgba{};
    auto segment=[](float x,float y,float ax,float ay,float bx,float by){
        float dx=bx-ax,dy=by-ay,t=std::clamp(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
        dx=x-ax-t*dx;dy=y-ay-t*dy;return std::sqrt(dx*dx+dy*dy);
    };
    for(int k=0;k<5;++k)for(int y=0;y<24;++y)for(int x=0;x<24;++x){
        unsigned coverage=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
            float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4,d=100;
            if(k==0)d=std::abs(std::hypot(px-12,py-12)-6.5f);
            if(k==1)d=std::min({segment(px,py,6,6,18,6),segment(px,py,18,6,18,18),segment(px,py,18,18,6,18),segment(px,py,6,18,6,6)});
            if(k==2)d=std::min({segment(px,py,12,5,19.5f,18.5f),segment(px,py,19.5f,18.5f,4.5f,18.5f),segment(px,py,4.5f,18.5f,12,5)});
            if(k==3)d=std::min(segment(px,py,7,7,17,17),segment(px,py,17,7,7,17));
            if(k<4)d=std::min(d,std::abs(std::hypot(px-12,py-12)-10.5f));
            if(k==4){const float ax=std::abs(px-12),ay=std::abs(py-12);
                if((ay>=4&&ay<=10&&ax<=(10-ay)*.7f)||(ax>=4&&ax<=10&&ay<=(10-ax)*.7f)||(ax<1.4f&&ay<1.4f))d=0;}
            if(d<=.8f)++coverage;
        }
        const size_t i=(y*120+k*24+x)*4;rgba[i]=214;rgba[i+1]=229;rgba[i+2]=242;rgba[i+3]=uint8_t(coverage*255/16);
    }
    return texture(120,24,rgba.data());
}
inline void launcher_face_button(Texture* atlas,int index,float x,float y){
    if(!atlas)return;const float u=index*.2f;
    Vertex v[]={{x,y,u,0,1,1,1,1},{x+24,y,u+.2f,0,1,1,1,1},{x,y+24,u,1,1,1,1,1},{x+24,y+24,u+.2f,1,1,1,1,1}};
    draw_quad(atlas,v);
}
inline Texture* make_launcher_keycaps(){
    std::array<uint8_t,160*28*4> rgba{};
    const int starts[]={0,28,56,104},widths[]={24,24,46,56};
    for(int k=0;k<4;++k)for(int y=0;y<28;++y)for(int x=0;x<widths[k];++x){
        unsigned coverage=0;const float radius=k<2?9.f:11.f;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
            const float px=x+(sx+.5f)/4,py=y+(sy+.5f)/4;
            const float qx=std::abs(px-widths[k]/2.f)-(widths[k]/2.f-1.5f-radius);
            const float qy=std::abs(py-14)-(12.f-radius);
            float d=std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-radius;
            if(k<2)d=std::max(d,py-23);
            if(std::abs(d)<=.65f)++coverage;
        }
        size_t i=(y*160+starts[k]+x)*4;rgba[i]=174;rgba[i+1]=199;rgba[i+2]=218;rgba[i+3]=uint8_t(coverage*255/16);
    }
    return texture(160,28,rgba.data());
}
inline void launcher_key_button(Texture* atlas,int index,float x,float y,const char* label){
    const float starts[]={0,28,56,104},widths[]={24,24,46,56};const float w=widths[index];
    if(atlas){const float u=starts[index]/160,uw=w/160;
        Vertex v[]={{x,y,u,0,1,1,1,1},{x+w,y,u+uw,0,1,1,1,1},{x,y+28,u,1,1,1,1,1},{x+w,y+28,u+uw,1,1,1,1,1}};
        draw_quad(atlas,v);}
    menu_text(x+(w-menu_text_width(label,13))/2,y+19,13,label,0xd6e5f2ff);
}
}
