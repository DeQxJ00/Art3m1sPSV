#include "gpu.hpp"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace direct {
namespace {
stbtt_fontinfo face{};std::vector<uint8_t> fontBytes;
struct Glyph{Texture* texture;int x,y,w,h,left,top,advance;};
struct Page{Texture* texture;std::vector<uint8_t> pixels;int x=1,y=1,row=0;bool dirty=true;};
std::unordered_map<uint64_t,Glyph> glyphs;std::vector<Page> pages;
constexpr int side=512;
uint32_t decode(const unsigned char*& p){uint32_t c=*p++;if(c<128)return c;unsigned n;
    if((c&0xe0)==0xc0){c&=31;n=1;}else if((c&0xf0)==0xe0){c&=15;n=2;}else if((c&0xf8)==0xf0){c&=7;n=3;}else return '?';
    while(n--){if((*p&0xc0)!=0x80)return '?';c=(c<<6)|(*p++&63);}return c;}
bool start(){if(!fontBytes.empty())return true;auto* f=std::fopen("app0:assets/menu.ttf","rb");if(!f)return false;
    std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::rewind(f);if(n<=0){std::fclose(f);return false;}
    fontBytes.resize(n);bool ok=std::fread(fontBytes.data(),1,n,f)==size_t(n);std::fclose(f);
    if(!ok||!stbtt_InitFont(&face,fontBytes.data(),stbtt_GetFontOffsetForIndex(fontBytes.data(),0))){fontBytes.clear();return false;}return true;}
void flush(){for(auto& p:pages)if(p.dirty){if(!p.texture)p.texture=texture(side,side,p.pixels.data());
    else if(!update(p.texture,p.pixels.data(),0,0,side,side))continue;p.dirty=false;}}
}
void menu_prepare(const char* text,float size){
    if(in_scene()||!text||!start())return;int px=std::clamp(int(size),8,64);float scale=stbtt_ScaleForPixelHeight(&face,float(px));
    const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(*p){auto c=decode(p);if(c=='\n')continue;uint64_t key=(uint64_t(px)<<32)|c;if(glyphs.count(key))continue;
        int w=0,h=0,left=0,top=0,advance=0,bearing=0;
        auto* bitmap=stbtt_GetCodepointBitmap(&face,scale,scale,c,&w,&h,&left,&top);
        stbtt_GetCodepointHMetrics(&face,c,&advance,&bearing);
        if(w&&h&&!bitmap)continue;
        if(pages.empty()||(pages.back().y+std::max(pages.back().row,h)+2>=side))pages.push_back({nullptr,std::vector<uint8_t>(side*side*4)});
        auto* page=&pages.back();if(page->x+w+2>=side){page->x=1;page->y+=page->row+2;page->row=0;}
        if(page->y+h+2>=side){pages.push_back({nullptr,std::vector<uint8_t>(side*side*4)});page=&pages.back();}
        if(!page->texture)page->texture=texture(side,side,page->pixels.data());
        int x=page->x,y=page->y;
        for(int row=0;row<h;row++)for(int col=0;col<w;col++){
            auto* d=&page->pixels[((row+y)*side+x+col)*4];d[0]=d[1]=d[2]=255;
            d[3]=bitmap[row*w+col];
        }
        page->x+=w+2;page->row=std::max(page->row,h);page->dirty=true;stbtt_FreeBitmap(bitmap,nullptr);
        if(page->texture)glyphs[key]={page->texture,x,y,w,h,left,-top,int(advance*scale+.5f)};
    }
    flush();
}
void menu_text(float x,float y,float size,const char* text,uint32_t color){
    if(!text)return;const float origin=x;const auto* p=reinterpret_cast<const unsigned char*>(text);int px=std::clamp(int(size),8,64);
    float r=(color>>24)/255.0f,g=((color>>16)&255)/255.0f,b=((color>>8)&255)/255.0f,a=(color&255)/255.0f;
    while(*p){auto c=decode(p);if(c=='\n'){x=origin;y+=size*1.4f;continue;}auto it=glyphs.find((uint64_t(px)<<32)|c);
        if(it==glyphs.end()){x+=size/2;continue;}auto& k=it->second;
        float dx=x+k.left,dy=y-k.top,u=float(k.x)/side,v=float(k.y)/side,uw=float(k.w)/side,vh=float(k.h)/side;
        Vertex verts[]={{dx,dy,u,v,r,g,b,a},{dx+k.w,dy,u+uw,v,r,g,b,a},{dx,dy+k.h,u,v+vh,r,g,b,a},{dx+k.w,dy+k.h,u+uw,v+vh,r,g,b,a}};
        if(k.w&&k.h)draw_quad(k.texture,verts);x+=k.advance;
    }
}
void menu_release(){if(!pages.empty()||!fontBytes.empty())log("menu font release: %u pages, %u glyphs, %u font bytes",unsigned(pages.size()),unsigned(glyphs.size()),unsigned(fontBytes.size()));
    for(auto& p:pages)destroy(p.texture);std::vector<Page>().swap(pages);std::unordered_map<uint64_t,Glyph>().swap(glyphs);std::vector<uint8_t>().swap(fontBytes);face={};}
}
