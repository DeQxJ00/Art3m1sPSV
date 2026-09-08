#include "gpu.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <cstdio>
#include <cstdarg>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
namespace direct { void log(const char* f,...){FILE* file=fopen("ux0:data/art3m1s-snapshot-probe.log","a");va_list args;va_start(args,f);if(file){vfprintf(file,f,args);fputc('\n',file);fclose(file);}va_end(args);} }
int main(){
    if(!direct::init())return 1;
    for(int i=0;i<6;i++){direct::begin();direct::rect(0,0,480,544,0xff0000ff);direct::rect(480,0,480,544,0x00ff00ff);direct::end();}
    auto* captured=direct::snapshot_completed();if(!captured)return 2;
    const uint8_t maskPixels[]={0,0,0,255,255,255,255,255};
    auto* mask=direct::texture(2,1,maskPixels);if(!mask)return 3;
    for(int i=0;i<300;i++){
        direct::begin();direct::rect(0,0,960,544,0x0000ffff);
        direct::Vertex q[]={{0,0,0,0,1,1,1,.5f},{960,0,1,0,1,1,1,.5f},{0,272,0,1,1,1,1,.5f},{960,272,1,1,1,1,1,.5f}};
        direct::draw_quad(captured,q);
        for(auto& v:q){v.y+=272;v.a=1;}
        direct::draw_quad(captured,q,direct::Alpha,nullptr,mask,.5f,.2f);
        direct::end();
    }
    direct::log("SNAPSHOT_PROBE top half alpha: left=(128,0,127), right=(0,128,127); bottom rule: left=blue, right=green");
    for(;;)sceKernelDelayThread(16000);
}
