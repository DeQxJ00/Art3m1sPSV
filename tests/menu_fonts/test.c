#include <assert.h>
#include <stdio.h>
#define NVG_NO_STB
#include "../../vendor/borealis/library/lib/extern/nanovg/nanovg.c"
static int widths[2048], heights[2048], next_image, live, fail_create;
static int create(void *p){(void)p;return 1;}
static int texture(void*p,int t,int w,int h,int f,const unsigned char*d){
    (void)p;(void)t;(void)f;(void)d;if(fail_create)return 0;
    int id=++next_image;assert(id<2048);widths[id]=w;heights[id]=h;live++;return id;
}
static int remove_texture(void*p,int id){(void)p;assert(widths[id]);widths[id]=0;live--;return 1;}
static int size(void*p,int id,int*w,int*h){(void)p;assert(widths[id]);*w=widths[id];*h=heights[id];return 1;}
static int update(void*p,int id,int x,int y,int w,int h,const unsigned char*d){(void)p;(void)x;(void)y;(void)w;(void)h;(void)d;assert(widths[id]);return 1;}
static void viewport(void*p,float w,float h,float r){(void)p;(void)w;(void)h;(void)r;}
static void noop(void*p){(void)p;}
static void triangles(void*p,NVGpaint*a,NVGcompositeOperationState c,NVGscissor*s,const NVGvertex*v,int n,float f){(void)p;(void)c;(void)s;(void)v;(void)n;(void)f;assert(widths[a->image]);}
int main(int argc,char**argv){
    assert(argc==2);
    NVGparams p={0};p.renderCreate=create;p.renderCreateTexture=texture;p.renderDeleteTexture=remove_texture;
    p.renderGetTextureSize=size;p.renderUpdateTexture=update;p.renderViewport=viewport;p.renderFlush=noop;p.renderTriangles=triangles;
    NVGcontext *vg=nvgCreateInternal(&p);assert(vg);
    unsigned char pixels[16]={0};int game=nvgCreateImageRGBA(vg,2,2,0,pixels);assert(game);
    float expected=0;
    for(int i=0;i<20;i++){
        int font=nvgCreateFont(vg,"menu",argv[1]);assert(font==0);
        nvgBeginFrame(vg,960,544,1);nvgFontFaceId(vg,font);nvgFontSize(vg,24);
        float advance=nvgText(vg,20,40,"Menu / Choose a game",NULL);
        assert(advance>20);if(i)assert(advance==expected);expected=advance;nvgEndFrame(vg);
        nvgBeginFrame(vg,960,544,1);nvgFontSize(vg,160);
        nvgText(vg,0,180,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",NULL);
        nvgEndFrame(vg);
        assert(vg->fs->params.width>NVG_INIT_FONTIMAGE_SIZE || vg->fs->params.height>NVG_INIT_FONTIMAGE_SIZE);
        assert(vg->fs->nfonts==1);
        fail_create=1;assert(!nvgResetFonts(vg));fail_create=0;
        assert(nvgFindFont(vg,"menu")==font); // Failed reset retains usable fonts.
        assert(nvgResetFonts(vg));assert(vg->fs->nfonts==0);
        assert(nvgFindFont(vg,"menu")==-1);assert(live==2&&widths[game]==2);
        assert(vg->fs->params.width==NVG_INIT_FONTIMAGE_SIZE&&vg->fs->params.height==NVG_INIT_FONTIMAGE_SIZE);
    }
    nvgDeleteImage(vg,game);nvgDeleteInternal(vg);assert(!live);
    puts("PASS: 20 font unload/reload cycles, stable handles/metrics, atlas release, game image preserved, allocation-failure rollback");
}
