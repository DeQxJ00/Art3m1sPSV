// Standalone test of the production direct renderer. No game/save access.
#include "gpu.hpp"
#include "texture_opacity.hpp"
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <vector>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
static FILE* output;
namespace direct { void log(const char* f,...){auto* file=fopen("ux0:data/art3m1s-direct-probe/result.log","a");va_list args;va_start(args,f);if(file){vfprintf(file,f,args);fputc('\n',file);fclose(file);}va_end(args);} }
struct Transform {float a=1,b=0,c=0,d=1,x=0,y=0;};
static void sprite(direct::Texture* t,Transform m,float x,float y,float w,float h,
    float u=0,float v=0,float uw=1,float vh=1,float r=1,float g=1,float b=1,float alpha=1,
    unsigned blend=0,const float* clip=nullptr,direct::Texture* rule=nullptr,float progress=0){
    float xs[]={x,x+w,x,x+w},ys[]={y,y,y+h,y+h};direct::Vertex q[4];
    for(int i=0;i<4;i++)q[i]={m.a*xs[i]+m.c*ys[i]+m.x,m.b*xs[i]+m.d*ys[i]+m.y,
        u+(i%2?uw:0),v+(i/2?vh:0),r,g,b,alpha};
    direct::draw_quad(t,q,blend,clip,rule,progress,.2f);
}
int main(){
    sceIoMkdir("ux0:data/art3m1s-direct-probe",0777);sceIoRemove("ux0:data/art3m1s-direct-probe/result.log");
    if(!direct::init())return 1;
    // Execute the actual ARM NEON branch, including block/tail boundaries and
    // each position of a single non-opaque texel. RGB bytes are not all 255.
    unsigned opacityChecks=0;
    for(unsigned n:{1u,15u,16u,63u,64u,65u,127u,128u,129u,1023u,1024u,1025u,2051u}){
        std::vector<uint8_t> data(size_t(n)*4+3,117);auto* p=data.data()+3;
        for(unsigned i=0;i<n;++i)p[i*4+3]=255;
        if(!direct::pixels_are_opaque(p,n))return 12;
        for(unsigned i=0;i<n;++i){p[i*4+3]=254;
            if(direct::pixels_are_opaque(p,n))return 13;
            p[i*4+3]=255;++opacityChecks;}
    }
    direct::log("OPACITY_ARM checks=%u passed",opacityChecks);
    std::vector<unsigned char> pixels(64*64*4);
    for(int y=0;y<64;y++)for(int x=0;x<64;x++){int i=(y*64+x)*4;pixels[i]=x*4;pixels[i+1]=y*4;pixels[i+2]=190;
        pixels[i+3]=x<4||y<4||x>=60||y>=60?0:((x/8+y/8)%3)*127;}
    auto* pattern=direct::texture(64,64,pixels.data());pixels.assign(1024*1024*4,0);
    for(int y=0;y<31;y++)for(int x=0;x<29;x++){int i=((y+787)*1024+x+927)*4;pixels[i]=pixels[i+1]=pixels[i+2]=255;
        pixels[i+3]=(x%7==2||y%9==3)?255:((x%7==1||y%9==2)?96:0);}
    auto* atlas=direct::texture(1024,1024,pixels.data());
    unsigned char rgba[]={200,100,50,128};auto* translucent=direct::texture(1,1,rgba);
    unsigned char mask[]={0,0,0,255,128,128,128,255,255,255,255,255};auto* rule=direct::texture(3,1,mask);
    // Odd width exercises GXM linear stride and partial region updates.
    pixels.assign(13*7*4,0);for(int y=0;y<7;y++)for(int x=0;x<13;x++){int i=(y*13+x)*4;pixels[i]=240;pixels[i+3]=255;}
    auto* changed=direct::texture(13,7,pixels.data());
    for(int y=2;y<5;y++)for(int x=4;x<9;x++){int i=(y*13+x)*4;pixels[i]=0;pixels[i+1]=210;}
    if(!direct::update(changed,pixels.data(),4,2,5,3))return 2;
    if(!pattern||!atlas||!translucent||!rule||!changed)return 3;
    // Same descriptor, independent metadata: imported alias forces the old
    // blending path. Both are compared side by side in the screenshot.
    auto* blendedOpaque=direct::import_texture(changed->descriptor);
    if(!changed->opaque||blendedOpaque->opaque||translucent->opaque)return 9;
    const unsigned char transparentPixel[]={200,100,50,0};
    const unsigned char opaquePixel[]={200,100,50,255};
    auto* replaced=direct::texture(1,1,opaquePixel);
    if(!replaced||!replaced->opaque||!direct::update(replaced,transparentPixel,0,0,1,1)||replaced->opaque)return 10;
    if(!direct::update(replaced,opaquePixel,0,0,1,1)||!replaced->opaque)return 11;
    direct::destroy(replaced);
    bool exported=false;unsigned frames=0;
    // Exercise the production queue before leaving the reference image visible.
    direct::begin();
    for(int glyph=0;glyph<40;glyph++)for(int part=0;part<6;part++)
        sprite(atlas,{},float(glyph*20),float(part),16,30,927.f/1024,787.f/1024,29.f/1024,31.f/1024,
            part==5?1:0,part==5?1:0,part==5?1:0);
    direct::end();auto counts=direct::last_frame_stats();
    direct::log("BATCH_TEXT quads=%u draws=%u uniforms=%u",counts.quads,counts.draws,counts.uniforms);
    if(counts.quads!=241||counts.draws!=2||counts.uniforms!=0)return 5;
    // Let display/readback complete a full buffer rotation in Vita3K before
    // checking the index endpoints (the game-frame capture remains separate).
    for(unsigned verificationFrame=0;verificationFrame<6;verificationFrame++){
        direct::begin();for(int i=0;i<16385;i++){
            if(i<16383)sprite(pattern,{},0,0,.125f,.125f); // Visible bounds, no covered sample: still exercises indices.
            else sprite(pattern,{},840+(i-16383)*40,430,32,32);
        }direct::end();
    }counts=direct::last_frame_stats();
    direct::log("BATCH_BOUNDARY quads=%u draws=%u uniforms=%u",counts.quads,counts.draws,counts.uniforms);
    if(counts.quads!=16386||counts.draws!=3||counts.uniforms!=0)return 6;
    direct::begin();sprite(pattern,{},40,40,64,64,0,0,1,1,1,1,1,0);
    sprite(pattern,{},-100,-100,32,32);direct::end();counts=direct::last_frame_stats();
    direct::log("CULL_INVISIBLE zero=%u outside=%u quads=%u",counts.zeroAlpha,counts.outside,counts.quads);
    if(counts.zeroAlpha!=1||counts.outside!=1||counts.quads!=1)return 8;
    // Vita3K CPU color memory can stay zero even while the GPU image is valid.
    // Validate index endpoints in the actual screenshot below, not that memory.
    for(;;){
        SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CROSS)break;
        direct::begin();direct::rect(0,0,960,544,0x1450b4ff);
        // Positions/assets match tests/image_quad/render.cpp and its captured baseline.
        for(int cell=0;cell<12;cell++){
            Transform m;m.x=(cell%3)*160+12;m.y=(cell/3)*136+12;
            float u=0,v=0,uw=1,vh=1,alpha=1,r=1,g=1,b=1;unsigned blend=0;float clip[4];bool clipped=false;
            auto* t=pattern;
            if(cell==1){r=.35f;g=.7f;b=.2f;alpha=.6f;}
            if(cell==2){u=v=1;uw=vh=-1;}
            if(cell==3){m.x+=128;m.a=-1;}
            if(cell==4){m.x+=14;m.y+=1;m.a=cosf(.17f);m.b=sinf(.17f);m.c=cosf(.17f)*tanf(.08f)-sinf(.17f);m.d=sinf(.17f)*tanf(.08f)+cosf(.17f);}
            if(cell==5){clip[0]=m.x+17;clip[1]=m.y+19;clip[2]=m.x+88;clip[3]=m.y+86;clipped=true;}
            if(cell==6){blend=1;alpha=.25f;}
            if(cell==7||cell==8){t=atlas;u=927.f/1024;v=787.f/1024;uw=29.f/1024;vh=31.f/1024;m.x+=.375f;m.y+=.3f;}
            if(cell==7)alpha=.25f;
            if(cell==9){m.y+=104;m.d=-1;}
            // The old cell 10 uses a rotated NanoVG scissor. This host accepts stage-space
            // axis-aligned clips, so this cell is a separate explicit hard-clip test.
            if(cell==10){clip[0]=m.x+17;clip[1]=m.y+14;clip[2]=m.x+100;clip[3]=m.y+81;clipped=true;m.x+=4;m.y+=3;m.a=m.d=cosf(.04f);m.b=sinf(.04f);m.c=-m.b;}
            auto draw=[&](float x,float y,float w,float h,float rr,float gg,float bb){sprite(t,m,x,y,w,h,u,v,uw,vh,rr,gg,bb,alpha,blend,clipped?clip:nullptr);};
            if(cell==6)for(int j=0;j<4;j++)draw(j*8,j*3,96,84,r,g,b);
            else if(cell==8){draw(0,3,112,96,.08f,.05f,.03f);draw(6,3,112,96,.08f,.05f,.03f);draw(3,0,112,96,.08f,.05f,.03f);draw(3,6,112,96,.08f,.05f,.03f);draw(3,3,112,96,.9f,.85f,.7f);}
            else if(cell==11){draw(0,0,112,96,r,g,b);direct::rect(m.x+18,m.y+18,30,30,0x46141ebe);draw(17,9,96,82,r,g,b);}
            else draw(0,0,112,96,r,g,b);
        }
        Transform identity;
        sprite(translucent,identity,500,20,100,80);
        sprite(translucent,identity,620,20,100,80,0,0,1,1,1,1,1,1,1);
        const float clip[]={760,40,800,70};sprite(translucent,identity,740,20,100,80,0,0,1,1,1,1,1,1,0,clip);
        for(int i=0;i<3;i++)sprite(direct::white(),identity,500,140+i*90,300,70,0,0,1,1,1,1,1,1,0,nullptr,rule,float(i)/2);
        sprite(changed,identity,500,430,130,70);
        const float ruleClip[]={700,440,900,490};
        for(int comparison=0;comparison<4;++comparison){
            float x=500+comparison*110.f;
            // Normal, faded, additive, and mirrored/tinted opaque draws.
            float a=comparison==1?.4f:1.f;unsigned blend=comparison==2?1:0;
            float u=comparison==3?1.f:0.f,uw=comparison==3?-1.f:1.f;
            for(int path=0;path<2;++path)sprite(path?blendedOpaque:changed,{},x+path*50,514,46,24,
                u,0,uw,1,.7f,.8f,.9f,a,blend);
        }
        sprite(direct::white(),identity,660,430,270,70,0,0,1,1,1,1,1,1,0,ruleClip,rule,.5f);
        for(int i=0;i<16385;i++){
            if(i<16383)sprite(pattern,{},0,0,.125f,.125f);
            else sprite(pattern,{},840+(i-16383)*40,110,32,32);
        }
        direct::end();
        if(++frames==60){pixels.resize(960*544*4);bool ok=direct::readback(960,544,pixels.data());
            auto* file=fopen("ux0:data/art3m1s-direct-probe/frame.rgba","wb");if(file){ok=ok&&fwrite(pixels.data(),1,pixels.size(),file)==pixels.size();fclose(file);}else ok=false;
            direct::log("DIRECT_PROBE frame=%u readback=%s",frames,ok?"OK":"FAILED");exported=ok;
            const unsigned sizes[][2]={{960,540},{320,180},{1200,680}};
            for(auto& size:sizes){unsigned w=size[0],h=size[1];std::vector<unsigned char> scaled(w*h*4);bool match=direct::readback(w,h,scaled.data());
                for(unsigned y=0;y<h&&match;y++)for(unsigned x=0;x<w;x++)
                    if(std::memcmp(scaled.data()+(size_t(y)*w+x)*4,pixels.data()+(size_t(y)*544/h*960+size_t(x)*960/w)*4,4)){match=false;break;}
                direct::log("READBACK_SCALE %ux%u exact=%u",w,h,match);if(!match)return 7;
            }}
    }
    direct::log("probe textures release begin");direct::destroy(changed);direct::destroy(rule);direct::destroy(translucent);direct::destroy(atlas);direct::destroy(pattern);direct::log("probe textures released");direct::prepare_process_exit();
    direct::log("DIRECT_PROBE clean_exit exported=%u",exported);if(output)fclose(output);sceKernelExitProcess(exported?0:4);return 0;
}
