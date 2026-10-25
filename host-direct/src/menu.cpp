#include "gpu.hpp"
#include "launcher_buttons.hpp"
#include "control_hints.hpp"
#include "fallback_menu.hpp"
#include "fallback_menu_atlas.hpp"
#include "fallback_menu_atlas_en.hpp"
#include <psp2/kernel/processmgr.h>
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
Texture* fallbackTexture=nullptr;
Texture* hintButtons=nullptr;Texture* hintKeycaps=nullptr;
Texture* fallbackButtons=nullptr;Texture* fallbackKeycaps=nullptr;
UiLanguage fallbackLanguage=UiLanguage::Chinese;
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
float menu_text_width(const char* text,float size){
    if(!text)return 0;float width=0;const int px=std::clamp(int(size),8,64);
    const auto* p=reinterpret_cast<const unsigned char*>(text);
    while(*p){const auto c=decode(p);const auto it=glyphs.find((uint64_t(px)<<32)|c);
        width+=it==glyphs.end()?size/2:it->second.advance;}
    return width;
}
void menu_text(float x,float y,float size,const char* text,uint32_t color){
    y+=menuTextOffsetY;
    if(!text)return;const float origin=x;const auto* p=reinterpret_cast<const unsigned char*>(text);int px=std::clamp(int(size),8,64);
    float r=(color>>24)/255.0f,g=((color>>16)&255)/255.0f,b=((color>>8)&255)/255.0f,a=(color&255)/255.0f;
    while(*p){auto c=decode(p);if(c=='\n'){x=origin;y+=size*1.4f;continue;}auto it=glyphs.find((uint64_t(px)<<32)|c);
        if(it==glyphs.end()){x+=size/2;continue;}auto& k=it->second;
        float dx=x+k.left,dy=y-k.top,u=float(k.x)/side,v=float(k.y)/side,uw=float(k.w)/side,vh=float(k.h)/side;
        Vertex verts[]={{dx,dy,u,v,r,g,b,a},{dx+k.w,dy,u+uw,v,r,g,b,a},{dx,dy+k.h,u,v+vh,r,g,b,a},{dx+k.w,dy+k.h,u+uw,v+vh,r,g,b,a}};
        if(k.w&&k.h)draw_quad(k.texture,verts);x+=k.advance;
    }
}
bool ui_control_prepare(const char* text,float size){
    const auto hints=control_hints(text);if(hints.empty())return false;
    if(in_scene())return false;
    if(!hintButtons)hintButtons=make_launcher_buttons();
    if(!hintKeycaps)hintKeycaps=make_launcher_keycaps();
    for(const auto& h:hints)for(int px=16;px<=int(hint_label_size(size));++px)menu_prepare(ui_translate(h.label.c_str()),float(px));
    for(auto label:{"L","R","START","SELECT"})menu_prepare(label,13);
    return true;
}
bool ui_control_draw(float x,float y,float size,const char* text,uint32_t color){
    const auto hints=control_hints(text);if(hints.empty()||!hintButtons||!hintKeycaps)return false;
    float pixels=hint_label_size(size),gap=20;
    auto width=[&](){float sum=0;for(const auto& h:hints)sum+=hint_icon_width(h.button)+(h.label.empty()?0:6+menu_text_width(ui_translate(h.label.c_str()),pixels));return sum;};
    const float available=std::max(0.f,std::min(928.f-x,x>=90&&x<=220?960.f-2*x:928.f-x));
    while(pixels>16&&width()+12*(hints.size()-1)>available)--pixels;
    if(hints.size()>1)gap=std::clamp((available-width())/float(hints.size()-1),8.f,24.f);
    for(const auto& h:hints){
        const auto b=h.button;
        if(b<=HintButton::Direction)launcher_face_button(hintButtons,int(b),x,y-19+menuTextOffsetY);
        else if(b==HintButton::Pages){launcher_key_button(hintKeycaps,0,x,y-21+menuTextOffsetY,"L");launcher_key_button(hintKeycaps,1,x+28,y-21+menuTextOffsetY,"R");}
        else{const int i=b==HintButton::Start?2:b==HintButton::Select?3:b==HintButton::L?0:1;launcher_key_button(hintKeycaps,i,x,y-21+menuTextOffsetY,i==0?"L":i==1?"R":i==2?"START":"SELECT");}
        const float w=hint_icon_width(b);if(!h.label.empty())menu_text(x+w+6,y,pixels,ui_translate(h.label.c_str()),color);
        x+=w+(h.label.empty()?0:6+menu_text_width(ui_translate(h.label.c_str()),pixels))+gap;
    }
    return true;
}
void image_progress_panel(const char* file,unsigned done,unsigned total,int stage){
    static uint64_t started=0,last=0;static std::string current;
    if(stage==3){present_progress_panel(nullptr,0,0);started=last=0;current.clear();return;}
    const auto now=sceKernelGetProcessTimeWide();const std::string name=file?file:"";
    if(!started||current!=name){started=now;last=0;current=name;}
    if(stage==1&&last&&now-last<150000)return;last=now;
    constexpr unsigned w=420,h=104;std::vector<uint8_t> panel(w*h*4);
    auto fill=[&](unsigned x,unsigned y,unsigned width,unsigned height,uint8_t r,uint8_t g,uint8_t b){
        for(unsigned row=y;row<std::min(h,y+height);++row)for(unsigned col=x;col<std::min(w,x+width);++col){auto* p=&panel[(row*w+col)*4];p[0]=r;p[1]=g;p[2]=b;p[3]=255;}};
    fill(0,0,w,h,20,29,43);fill(0,0,w,3,73,204,173);
    auto text=[&](const char* value,int baseline,int pixels){
        if(!start())return;const float scale=stbtt_ScaleForPixelHeight(&face,float(pixels));int x=12;
        const auto* p=reinterpret_cast<const unsigned char*>(value);
        while(*p){const auto c=decode(p);int bw=0,bh=0,left=0,top=0,advance=0,bearing=0;
            stbtt_GetCodepointHMetrics(&face,c,&advance,&bearing);if(x+int(advance*scale)>int(w)-12)break;
            auto* bitmap=stbtt_GetCodepointBitmap(&face,scale,scale,c,&bw,&bh,&left,&top);
            if(bitmap)for(int yy=0;yy<bh;++yy)for(int xx=0;xx<bw;++xx){int dx=x+left+xx,dy=baseline+top+yy;if(dx<0||dy<0||dx>=int(w)||dy>=int(h))continue;
                unsigned a=bitmap[yy*bw+xx];auto* out=&panel[(dy*w+dx)*4];for(unsigned k=0;k<3;++k)out[k]=uint8_t((out[k]*(255-a)+235*a+127)/255);}
            stbtt_FreeBitmap(bitmap,nullptr);x+=int(advance*scale+.5f);
        }
    };
    const char* label=stage==4?"正在解码图片":stage==0?"正在准备图片":stage==1?"正在压缩图片":"正在保存纹理缓存";
    text(ui_translate(label),25,20);
    auto slash=name.find_last_of("/\\");auto leaf=slash==name.npos?name:name.substr(slash+1);text(leaf.c_str(),49,17);
    const float fraction=total?std::clamp(float(done)/total,0.f,1.f):0;
    char detail[80];if(stage==1)std::snprintf(detail,sizeof(detail),"%u%%   %.1f s",unsigned(fraction*100),double(now-started)/1000000);
    else std::snprintf(detail,sizeof(detail),"...   %.1f s",double(now-started)/1000000);text(detail,73,17);
    fill(12,85,396,7,48,65,81);fill(12,85,unsigned(396*fraction),7,73,204,173);
    present_progress_panel(panel.data(),w,h);
}
void menu_release(){if(hintButtons)destroy(hintButtons);hintButtons=nullptr;if(hintKeycaps)destroy(hintKeycaps);hintKeycaps=nullptr;if(!pages.empty()||!fontBytes.empty())log("menu font release: %u pages, %u glyphs, %u font bytes",unsigned(pages.size()),unsigned(glyphs.size()),unsigned(fontBytes.size()));
    for(auto& p:pages)destroy(p.texture);std::vector<Page>().swap(pages);std::unordered_map<uint64_t,Glyph>().swap(glyphs);std::vector<uint8_t>().swap(fontBytes);face={};}

bool fallback_menu_prepare(){
    if(in_scene())return false;
    if(fallbackTexture&&fallbackLanguage!=uiLanguage)fallback_menu_release();
    if(!fallbackButtons)fallbackButtons=make_launcher_buttons();
    if(!fallbackKeycaps)fallbackKeycaps=make_launcher_keycaps();
    if(fallbackTexture)return true;
    const auto started=sceKernelGetProcessTimeWide();
    const bool english=uiLanguage==UiLanguage::English;
    const auto* rle=english?fallback_atlas_en::rle:fallback_atlas::rle;
    const auto rleSize=english?sizeof(fallback_atlas_en::rle):sizeof(fallback_atlas::rle);
    const unsigned width=english?fallback_atlas_en::width:fallback_atlas::width;
    const unsigned height=english?fallback_atlas_en::height:fallback_atlas::height;
    static_assert(sizeof(fallback_atlas_en::regions)/sizeof(fallback_atlas_en::regions[0])==unsigned(FallbackLabel::Count));
    const size_t count=width*height;
    static_assert(sizeof(fallback_atlas::regions)/sizeof(fallback_atlas::regions[0])==unsigned(FallbackLabel::Count));
    static_assert(sizeof(fallback_atlas::rle)%2==0);
    // Only a temporary staging copy. No font file, rasterizer or glyph cache is
    // touched by the label atlas, including its pre-rendered control hints.
    // Opening uploads the atlas; closing the menu releases it.
    std::vector<uint8_t> rgba(count*4,255);size_t pixel=0;
    for(size_t i=0;i<rleSize;i+=2){
        const auto run=rle[i];
        if(!run||pixel+run>count)return false;
        for(unsigned n=0;n<run;n++)rgba[(pixel++)*4+3]=rle[i+1];
    }
    if(pixel!=count)return false;
    fallbackTexture=texture(width,height,rgba.data());
    log("[host-menu-atlas] ready=%d rgba_bytes=%u embedded_bytes=%u prepare_us=%llu; embedded labels with shared control hints",
        int(fallbackTexture!=nullptr),unsigned(count*4),unsigned(rleSize),
        (unsigned long long)(sceKernelGetProcessTimeWide()-started));
    fallbackLanguage=uiLanguage;
    return fallbackTexture!=nullptr;
}
void fallback_menu_text(float x,float y,FallbackLabel label,uint32_t color){
    if(label==FallbackLabel::FontHelp||label==FallbackLabel::Help){
        const FallbackLabel hints[]={FallbackLabel::HintSelect,FallbackLabel::HintAdjust,FallbackLabel::HintConfirm,FallbackLabel::HintPage,FallbackLabel::HintBack};
        const int icons[]={4,4,0,-1,3};
        for(unsigned i=0;i<5;++i){
            if((i==1||i==3)&&label==FallbackLabel::Help)continue;
            const float iconWidth=i==3?58.f:30.f;
            if(i==3){fallback_menu_page_button(x,y-21,false);fallback_menu_page_button(x+28,y-21,true);}
            else launcher_face_button(fallbackButtons,icons[i],x,y-19);
            fallback_menu_text(x+iconWidth,y,hints[i],color);
            const auto index=unsigned(hints[i]);
            const auto width=fallbackLanguage==UiLanguage::English?fallback_atlas_en::regions[index].w:fallback_atlas::regions[index].w;
            x+=iconWidth+width+20;
        }
        return;
    }
    y+=kMenuTextOffsetY;
    if(!fallbackTexture||unsigned(label)>=unsigned(FallbackLabel::Count))return;
    auto k=fallback_atlas::regions[unsigned(label)];
    if(fallbackLanguage==UiLanguage::English){const auto& e=fallback_atlas_en::regions[unsigned(label)];k={e.x,e.y,e.w,e.h,e.left,e.top};}
    float r=(color>>24)/255.0f,g=((color>>16)&255)/255.0f,b=((color>>8)&255)/255.0f,a=(color&255)/255.0f;
    const unsigned width=fallbackLanguage==UiLanguage::English?fallback_atlas_en::width:fallback_atlas::width;
    const unsigned height=fallbackLanguage==UiLanguage::English?fallback_atlas_en::height:fallback_atlas::height;
    float dx=x+k.left,dy=y+k.top,u=float(k.x)/width,v=float(k.y)/height;
    float uw=float(k.w)/width,vh=float(k.h)/height;
    Vertex verts[]={{dx,dy,u,v,r,g,b,a},{dx+k.w,dy,u+uw,v,r,g,b,a},
        {dx,dy+k.h,u,v+vh,r,g,b,a},{dx+k.w,dy+k.h,u+uw,v+vh,r,g,b,a}};
    draw_quad(fallbackTexture,verts);
}
void fallback_menu_page_button(float x,float y,bool next){
    if(!fallbackKeycaps)return;
    float u=next?28.f/160.f:0,uw=24.f/160.f;
    Vertex v[]={{x,y,u,0,1,1,1,1},{x+24,y,u+uw,0,1,1,1,1},{x,y+28,u,1,1,1,1},{x+24,y+28,u+uw,1,1,1,1}};
    draw_quad(fallbackKeycaps,v);
    fallback_menu_text(x+8,y+19-kMenuTextOffsetY,next?FallbackLabel::KeyR:FallbackLabel::KeyL,0xd6e5f2ff);
}
void fallback_menu_release(){
    if(fallbackKeycaps){destroy(fallbackKeycaps);fallbackKeycaps=nullptr;}
    if(fallbackButtons){destroy(fallbackButtons);fallbackButtons=nullptr;}
    if(fallbackTexture){destroy(fallbackTexture);fallbackTexture=nullptr;log("[host-menu-atlas] released");}
}
}
