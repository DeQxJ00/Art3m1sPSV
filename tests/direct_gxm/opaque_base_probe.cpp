#include "gpu.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
extern "C" {unsigned int _newlib_heap_size_user=64*1024*1024;}
namespace direct {void log(const char* fmt,...){
    auto* f=fopen("ux0:data/art3m1s-opaque-base-probe/result.log","a");if(!f)return;
    va_list ap;va_start(ap,fmt);vfprintf(f,fmt,ap);va_end(ap);fputc('\n',f);fclose(f);
}}
static void children(direct::Texture* t,float x){
    const float clip[]={x+13,31,x+469,510};
    for(unsigned j=0;j<4;j++){
        const float y=35.25f+j*113;
        direct::rect(x+21.25f,y,301.5f,92.5f,0x2488cc80);
        direct::rect(x+167.5f,y+11.25f,244.25f,57.5f,0xe08a38a3);
        const float a[]={0.f,0.23f,0.71f,1.f};
        direct::Vertex q[]={{x+4.25f,y+9,0,0,0.8f,1,0.6f,a[j]},
            {x+380.5f,y-2.25f,1,0,0.8f,1,0.6f,a[j]},
            {x+53.5f,y+103,0,1,0.8f,1,0.6f,a[j]},
            {x+429.75f,y+91.75f,1,1,0.8f,1,0.6f,a[j]}};
        direct::draw_quad(t,q,0,clip);
    }
}
int main(){
    sceIoMkdir("ux0:data/art3m1s-opaque-base-probe",0777);
    if(!direct::init())return 1;
    direct::set_deferred_finish(true);
    std::vector<uint8_t> p(64*64*4);
    for(unsigned i=0;i<64*64;i++){p[i*4]=i*37;p[i*4+1]=i*53;p[i*4+2]=i*71;p[i*4+3]=i;}
    auto* texture=direct::texture(64,64,p.data());if(!texture)return 2;
    for(unsigned frame=0;frame<1800;frame++){
        direct::begin();direct::rect(0,0,960,544,0x71a937ff);
        if(!direct::group_begin())return 3;
        children(texture,0);
        direct::EffectDraw d{};d.tint[0]=d.tint[1]=d.tint[2]=d.tint[3]=1;
        d.effects.flags[0]=3;d.effects.transition[2]=1;d.blend=5;
        d.hasClip=1;d.clip[2]=480;d.clip[3]=544;
        direct::group_end(d,nullptr,1,1);
        // The candidate replaces full-stage isolation with this opaque base.
        // Offset a matching panel so emulator screenshots are the pixel oracle.
        direct::rect(480,0,480,544,0x000000ff);children(texture,480);
        direct::end();
        if(frame==2)direct::log("READY left=forced-opaque-group right=black-source-over; compare 0..479 vs 480..959 all rows");
    }
    direct::wait();direct::destroy(texture);direct::prepare_process_exit();
    direct::log("DONE");sceKernelExitProcess(0);return 0;
}
