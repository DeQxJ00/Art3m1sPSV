// Exercises production host-direct GPU code, without game assets or saves.
#include "gpu.hpp"
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstdarg>
#include <cmath>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
namespace direct {void log(const char* f,...){FILE* p=fopen("ux0:data/art3m1s-builtin-probe.log","a");va_list a;va_start(a,f);if(p){vfprintf(p,f,a);fputc('\n',p);fclose(p);}va_end(a);}}
using namespace direct;
static void patch(Texture* t,float x,float y,const BuiltinEffects& e={},unsigned blend=0,Texture* mask=nullptr,float alpha=1){
    Vertex v[]={{x,y,0,0,1,1,1,alpha},{x+100,y,1,0,1,1,1,alpha},{x,y+90,0,1,1,1,1,alpha},{x+100,y+90,1,1,1,1,1,alpha}};
    draw_builtin(t,v,4,false,blend,nullptr,mask,e);
}
static EffectDraw composite(float kind=3){
    EffectDraw d{};d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1;d.effects.flags[0]=kind;d.blend=5;return d;
}
int main(){
    sceIoRemove("ux0:data/art3m1s-builtin-probe.log");if(!init())return 1;
    const uint8_t red[]={200,100,50,255},half[]={200,100,50,128},m[]={255,255,255,128};
    auto* t=texture(1,1,red);auto* h=texture(1,1,half);auto* mask=texture(1,1,m);if(!t||!h||!mask)return 2;
    Texture* snapshot=nullptr;
    for(unsigned frame=0;;frame++){
        begin();rect(0,0,960,544,0x204060ff);
        if(frame==3){snapshot=capture_completed_texture();if(!snapshot)return 9;}
        BuiltinEffects gray;gray.flags[1]=1;BuiltinEffects neg;neg.flags[2]=1;
        patch(t,20,20);patch(t,140,20,gray);patch(t,260,20,neg);gray.flags[2]=1;patch(t,380,20,gray);
        BuiltinEffects rule;rule.flags[0]=1;rule.transition[0]=.5f;rule.transition[1]=.2f;
        patch(t,500,20,rule,0,mask);patch(h,620,20);patch(h,740,20,neg);
        // Parent pixels must survive EndScene/BeginScene, including after an
        // inner group and after a previously used pool slot is cleared.
        if(!group_begin())return 3;
        patch(t,20,150);patch(h,140,150);
        auto g=composite();g.effects.flags[1]=1;group_end(g,nullptr,1,1);
        if(!group_begin())return 4;
        patch(t,260,150);
        if(!group_begin())return 5;
        patch(h,380,150);g=composite();g.effects.flags[2]=1;group_end(g,nullptr,1,1);
        g=composite();g.tint[3]=.5f;group_end(g,nullptr,1,1);
        if(!group_begin())return 6;
        patch(t,500,150);
        if(!group_mask_begin())return 7;
        patch(mask,500,150);
        g=composite(2);group_end(g,nullptr,1,1);
        // A fresh pool slot must not retain the colored patch at x=500.
        if(!group_begin())return 8;
        patch(h,620,150);g=composite();g.effects.flags[2]=1;group_end(g,nullptr,1,1);
        // Fully transparent pixels must stay transparent under negative.
        rect(740,150,100,90,0x00ff00ff);
        // Nontrivial blends against a known background.
        for(unsigned b=1;b<=9;b++){float x=20+(b-1)*102;patch(h,x,280,{},b);}
        // Clip + grayscale, expanded mesh, and ordinary draw after effects.
        Vertex q[]={{20,410,0,0,1,1,1,1},{120,410,1,0,1,1,1,1},{20,500,0,1,1,1,1,1},{120,500,1,1,1,1,1,1}};
        float clip[]={20,410,70,500};gray.flags[2]=0;draw_builtin(t,q,4,false,0,clip,nullptr,gray);
        Vertex tri[]={{160,410,0,0,1,1,1,1},{260,410,1,0,1,1,1,1},{160,510,0,1,1,1,1,1}};
        draw_builtin(t,tri,3,true,0,nullptr,nullptr,neg);
        rect(300,410,100,90,0xff00ffff);
        if(snapshot){
            Vertex copy[]={{430,410,20.f/960,20.f/544,1,1,1,1},{530,410,120.f/960,20.f/544,1,1,1,1},
                {430,500,20.f/960,110.f/544,1,1,1,1},{530,500,120.f/960,110.f/544,1,1,1,1}};
            draw_builtin(snapshot,copy,4,false,10,nullptr,nullptr,{});
        }
        end();if(frame==2)log("BUILTIN_PROBE all groups, mask, blends, mesh submitted");
    }
}
