#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace direct {
struct Vertex { float x,y,u,v,r,g,b,a; };
// Source texel bounds, exclusive at the right/bottom. Imported video descriptors
// have no CPU alpha data and leave known=false. Updates only expand the bounds:
// erasing pixels may reduce the benefit but can never cut newly visible pixels.
struct AlphaBounds {
    unsigned left=0,top=0,right=0,bottom=0;
    bool known=false;
    void include(const uint8_t* rgba,unsigned width,unsigned x,unsigned y,unsigned w,unsigned h) {
        if(!known){left=width;top=y+h;right=bottom=0;known=true;}
        // Existing bounds are conservative and only expand. Pixels inside them
        // cannot change the result, including erased/replaced video frames.
        if(x>=left&&y>=top&&x+w<=right&&y+h<=bottom)return;
        for(unsigned row=y;row<y+h;row++){
            const auto* p=rgba+size_t(row)*width*4;
            if(row>=top&&row<bottom){
                include_span(p,row,x,std::min(x+w,left));
                include_span(p,row,std::max(x,right),x+w);
            }else{
                include_span(p,row,x,x+w);
            }
        }
    }
private:
    void include_span(const uint8_t* row,unsigned y,unsigned x,unsigned end){
        // Only the first/last nonzero alpha in a row can expand its bounds.
        // Avoid touching the opaque interior of backgrounds and captures.
        while(x<end&&!row[size_t(x)*4+3])++x;
        if(x>=end)return;
        unsigned last=end-1;
        while(last>x&&!row[size_t(last)*4+3])--last;
        left=std::min(left,x);right=std::max(right,last+1);
        top=std::min(top,y);bottom=std::max(bottom,y+1);
    }
};
enum class QuadResult { Draw, ZeroAlpha, Outside, Empty };
inline double screen_area(const Vertex* q) {
    float x0=q[0].x,x1=x0,y0=q[0].y,y1=y0;
    for(unsigned i=1;i<4;i++){x0=std::min(x0,q[i].x);x1=std::max(x1,q[i].x);y0=std::min(y0,q[i].y);y1=std::max(y1,q[i].y);}
    return double(std::max(0.f,std::min(960.f,x1)-std::max(0.f,x0)))*
        std::max(0.f,std::min(544.f,y1)-std::max(0.f,y0));
}
inline QuadResult trim_quad(Vertex* q,const AlphaBounds& bounds,unsigned width,unsigned height,bool& trimmed) {
    trimmed=false;
    bool zero=true;for(unsigned i=0;i<4;i++)zero&=q[i].a==0;
    if(zero)return QuadResult::ZeroAlpha;
    // Do not classify malformed coordinates as invisible based on NaN ordering.
    for(unsigned i=0;i<4;i++)if(!std::isfinite(q[i].x)||!std::isfinite(q[i].y))return QuadResult::Draw;
    if(screen_area(q)==0)return QuadResult::Outside;
    if(!bounds.known||!width||!height)return QuadResult::Draw;
    if(bounds.right==0||bounds.bottom==0)return QuadResult::Empty;
    // Restrict cropping to affine axis-aligned sprites with a rectangular UV
    // region and constant tint. Rotation/shear and vertex gradients keep the
    // original geometry. UVs outside the image keep CLAMP sampler semantics.
    if(q[0].x!=q[2].x||q[1].x!=q[3].x||q[0].y!=q[1].y||q[2].y!=q[3].y||
       q[0].u!=q[2].u||q[1].u!=q[3].u||q[0].v!=q[1].v||q[2].v!=q[3].v)return QuadResult::Draw;
    for(unsigned i=0;i<4;i++)if(!(q[i].u>=0&&q[i].u<=1&&q[i].v>=0&&q[i].v<=1)||
       q[i].r!=q[0].r||q[i].g!=q[0].g||q[i].b!=q[0].b||q[i].a!=q[0].a)return QuadResult::Draw;
    const float du=q[1].u-q[0].u,dv=q[2].v-q[0].v;
    if(du==0||dv==0)return QuadResult::Draw;
    // One transparent texel on each side preserves the entire bilinear filter
    // footprint, including subpixel movement and fractional/negative UV spans.
    float u0=float(bounds.left?bounds.left-1:0)/width,u1=float(std::min(width,bounds.right+1))/width;
    float v0=float(bounds.top?bounds.top-1:0)/height,v1=float(std::min(height,bounds.bottom+1))/height;
    float ax=(u0-q[0].u)/du,bx=(u1-q[0].u)/du,ay=(v0-q[0].v)/dv,by=(v1-q[0].v)/dv;
    if(ax>bx)std::swap(ax,bx);if(ay>by)std::swap(ay,by);
    ax=std::max(0.f,ax);bx=std::min(1.f,bx);ay=std::max(0.f,ay);by=std::min(1.f,by);
    if(ax>=bx||ay>=by)return QuadResult::Empty;
    if(ax==0&&bx==1&&ay==0&&by==1)return QuadResult::Draw;
    const Vertex origin=q[0];const float dx=q[1].x-origin.x,dy=q[2].y-origin.y;
    for(unsigned i=0;i<4;i++){const float x=i%2?bx:ax,y=i/2?by:ay;
        q[i].x=origin.x+dx*x;q[i].y=origin.y+dy*y;q[i].u=origin.u+du*x;q[i].v=origin.v+dv*y;}
    trimmed=true;return QuadResult::Draw;
}
}
