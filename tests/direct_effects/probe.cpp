// Isolated synthetic GPU probe: no access to games, saves or main host logs.
#include "gpu.hpp"
#include "effects_bridge.hpp"
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
extern "C" int art3m1s_gxm_upload_texture(uint64_t,uint32_t,uint32_t,const uint8_t*,size_t);
namespace direct { void log(const char* f,...){auto* out=fopen("ux0:data/art3m1s-effects-probe/result.log","a");va_list a;va_start(a,f);if(out){vfprintf(out,f,a);fputc('\n',out);fclose(out);}va_end(a);} }
void draw(direct::Texture* t,int cell,unsigned blend=0,const direct::Effects* effects=nullptr,direct::Texture* mask=nullptr,
          float r=1,float g=1,float b=1,float a=1,const float* clip=nullptr){
    float x=(cell%12)*80+4,y=(cell/12)*68+4;
    direct::Vertex q[]={{x,y,0,0,r,g,b,a},{x+72,y,1,0,r,g,b,a},{x,y+60,0,1,r,g,b,a},{x+72,y+60,1,1,r,g,b,a}};
    direct::draw_quad(t,q,blend,clip,mask,0,1.f/255,effects);
}
int main(){
    sceIoMkdir("ux0:data/art3m1s-effects-probe",0777);
    {auto* f=fopen("ux0:data/art3m1s-effects-probe/result.log","w");if(f)fclose(f);}
    if(!direct::init())return 1;
    const uint8_t source[]={180,90,30,128}, premult[]={90,45,15,128}, maskPixel[]={128,128,128,128},transparent[]={0,0,0,0};
    auto* t=direct::texture(1,1,source);auto* pm=direct::texture(1,1,premult);
    auto* mask=direct::texture(1,1,maskPixel);auto* empty=direct::texture(1,1,transparent);
    if(!t||!pm||!mask||!empty)return 2;
    if(!art3m1s_gxm_upload_texture(1000,1,1,source,4)||!art3m1s_gxm_upload_texture(1001,1,1,maskPixel,4)||
        !art3m1s_gxm_upload_texture(1002,1,1,premult,4))return 8;
    const uint8_t opaqueColors[][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{180,90,30,255}};
    for(int i=0;i<4;i++)if(!art3m1s_gxm_upload_texture(1100+i,1,1,opaqueColors[i],4))return 9;
    for(int frame=0;frame<300;frame++){
        direct::begin();direct::rect(0,0,960,544,0x1e5a96ff);
        for(int filter=0;filter<4;filter++){
            direct::Effects e;e.flags[1]=filter&1;e.flags[2]=(filter>>1)&1;
            draw(t,filter,0,&e,nullptr,.6f,.8f,.4f,.7f);
        }
        // Opaque colors distinguish grayscale itself from colored-background
        // alpha blending; alternating flags catch stale uniform/batch state.
        for(int i=0;i<8;i++){
            int cell=4+i;EffectDraw d{};d.texture=1100+(i<3?i:3);
            d.transform[0]=d.transform[3]=1;d.transform[4]=cell*80+4;d.transform[5]=4;
            d.quadSize[0]=72;d.quadSize[1]=60;d.uv[2]=d.uv[3]=1;for(auto& v:d.tint)v=1;
            d.effects.flags[1]=(i!=4&&i!=6);d.effects.flags[2]=i==7;
            art3m1s_gxm_draw_effect(&d);
        }
        for(unsigned mode=0;mode<10;mode++)draw(t,12+mode,mode);
        for(unsigned mode=0;mode<10;mode++){
            if(!direct::group_begin())return 3;
            draw(t,24+mode);draw(t,24+mode,mode);
            direct::Effects e;e.flags[0]=direct::GroupComposite;
            direct::group_end(e,direct::PremultipliedAlpha,nullptr,nullptr,1,1,1,1);
        }
        for(unsigned filter=0;filter<4;filter++){
            direct::Effects e;e.flags[0]=direct::GroupComposite;e.flags[1]=filter&1;e.flags[2]=(filter>>1)&1;
            draw(pm,36+filter,direct::PremultipliedAlpha,&e,mask,.6f,.8f,.4f,.7f);
        }
        direct::Effects e;e.flags[0]=direct::AlphaMask;draw(pm,40,direct::PremultipliedAlpha,&e,mask,1,1,1,.7f);
        e.flags[0]=direct::GroupComposite;e.transition[2]=1;draw(pm,41,direct::PremultipliedAlpha,&e,mask);
        draw(empty,42,direct::PremultipliedAlpha,&e,mask);e.flags[2]=1;draw(empty,43,direct::PremultipliedAlpha,&e,mask);
        for(int step=0;step<3;step++){
            e={};e.flags[0]=direct::Rule;e.transition[0]=step*.5f;e.transition[1]=.2f;
            draw(t,48+step,0,&e,mask);
        }
        float clip[]={4*80+40,4*68+4,5*80-4,5*68-4};
        e={};e.flags[1]=e.flags[2]=1;draw(t,52,0,&e,nullptr,1,1,1,1,clip);
        clip[0]+=80;clip[2]+=80;e.flags[0]=direct::AlphaMask;draw(pm,53,direct::PremultipliedAlpha,&e,mask,1,1,1,1,clip);
        for(int mode=0;mode<8;mode++){
            e={};e.flags[3]=1;e.modelClip[2]=e.modelClip[3]=10000;
            for(auto& c:e.corners)c=1;
            if(mode==1){for(int i=0;i<4;i++){e.corners[i*4]=i%2?1:.2f;e.corners[i*4+1]=i/2?1:.3f;}}
            if(mode==2){e.wipe[0]=1;e.wipe[1]=-.25f;e.wipe[2]=1;}
            if(mode==3)e.modelX[3]=1;
            if(mode==4)e.wipe[3]=3;
            if(mode==5)e.wipe[3]=5;
            if(mode==6){e.flags[1]=e.flags[2]=1;e.modelX[3]=1;}
            if(mode==7){e.wipe[0]=0;e.wipe[1]=0;e.wipe[2]=1;}
            draw(t,60+mode,0,&e);
        }
        if(!direct::group_begin())return 4;
        draw(t,72);if(!direct::group_begin())return 5;draw(t,72);
        e={};e.flags[0]=direct::GroupComposite;e.flags[1]=1;
        direct::group_end(e,direct::PremultipliedAlpha,nullptr,nullptr,1,1,1,.5f);
        if(!direct::group_mask_begin())return 6;draw(mask,72);
        e={};e.flags[0]=direct::AlphaMask;
        direct::group_end(e,direct::PremultipliedAlpha,nullptr,nullptr,1,1,1,1);
        if(!direct::group_begin())return 7;draw(t,frame%2?74:73);
        e={};e.flags[0]=direct::GroupComposite;
        direct::group_end(e,direct::PremultipliedAlpha,nullptr,nullptr,1,1,1,1);
        draw(t,75);draw(empty,75,direct::Copy);
        // Exercise the real Rust/C layout and bridge, including model-space
        // reconstruction and mesh UVs, rather than only calling gpu.cpp.
        for(int i=0;i<12;i++){
            int cell=84+i;EffectDraw d{};d.texture=1000;d.transform[0]=d.transform[3]=1;
            d.transform[4]=(cell%12)*80+4;d.transform[5]=(cell/12)*68+4;
            d.quadSize[0]=72;d.quadSize[1]=60;d.uv[2]=d.uv[3]=1;for(auto& v:d.tint)v=1;
            if(i<3){d.effects.flags[1]=i!=1;d.effects.flags[2]=i!=0;}
            if(i==3||i==5||i==10||i==11){
                d.effects.flags[3]=1;for(auto& v:d.effects.corners)v=1;
                if(i==3)d.effects.modelClip[0]=.5f;
                if(i==5){d.effects.modelClip[2]=36;d.effects.modelClip[3]=60;}
                if(i==10){d.uv[0]=1;d.uv[2]=-1;for(int k=0;k<4;k++)d.effects.corners[k*4]=k%2?1:.2f;}
                if(i==11){d.effects.wipe[2]=1;d.effects.wipe[0]=0;}
            }
            const float triangle[]={0,0,0,0,72,0,1,0,0,60,0,1};
            if(i==4||i==5){d.mesh=triangle;d.meshCount=3;}
            if(i==6){d.effects.flags[0]=direct::Rule;d.effects.transition[0]=1;}
            if(i==7){d.effects.flags[1]=1;d.blend=direct::Multiply;}
            if(i==8){d.texture=1002;d.mask=1001;d.effects.flags[0]=direct::AlphaMask;d.blend=direct::PremultipliedAlpha;}
            if(i==9){d.texture=1002;d.effects.flags[0]=direct::GroupComposite;d.effects.flags[1]=d.effects.flags[2]=1;d.blend=direct::PremultipliedAlpha;}
            art3m1s_gxm_draw_effect(&d);
        }
        direct::end();
        if(frame==299){auto stats=direct::last_frame_stats();direct::log("EFFECTS_PROBE complete frames=300 draws=%u uniforms=%u",stats.draws,stats.uniforms);}
    }
    for(;;){SceCtrlData pad{};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CROSS)break;sceKernelDelayThread(16000);}
    direct::prepare_process_exit();sceKernelExitProcess(0);return 0;
}
