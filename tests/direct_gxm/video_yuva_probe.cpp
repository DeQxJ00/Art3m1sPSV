#include "gpu.hpp"
#include "video_convert.h"
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <cstdarg>
#include <vector>
#include <algorithm>
#include <cstdlib>
extern "C" { unsigned int _newlib_heap_size_user=64*1024*1024; }
namespace direct {void log(const char* f,...){
    FILE* out=fopen("ux0:data/art3m1s-yuva-probe/result.log","a");
    if(!out)return;va_list ap;va_start(ap,f);vfprintf(out,f,ap);va_end(ap);fputc('\n',out);fclose(out);
}}
static void pair(direct::Texture* cpu,direct::Texture* gpu,bool native=false){
    direct::begin();direct::rect(0,0,960,544,0x284868ff);
    for(unsigned i=0;i<2;i++){
        float x=i?504:8;
        float w=native?96:448,h=native?64:448;
        direct::Vertex q[]={{x,48,0,0,1,1,1,1},{x+w,48,1,0,1,1,1,1},
            {x,48+h,0,1,1,1,1,1},{x+w,48+h,1,1,1,1,1,1}};
        direct::draw_quad(i?gpu:cpu,q);
    }
    direct::end();
}
int main(){
    sceIoMkdir("ux0:data/art3m1s-yuva-probe",0777);
    FILE* f=fopen("ux0:data/art3m1s-yuva-probe/result.log","w");if(!f)return 1;fclose(f);
    if(!direct::init())return 2;
    if(!direct::set_deferred_finish(true))return 6;
    direct::log("YUVA_PROBE deferred display enabled; rotate mapped input slots and copied inputs, output fence retained");
    const unsigned w=96,h=64,n=w*h;
    std::vector<uint8_t> p(n*4),ref(n*4),alpha(n);
    direct::Texture *gpu=nullptr,*cpu=nullptr;
    uint8_t* slots[3]{};
    if(!direct::video_yuva_queue_open(w,h,slots,3))return 7;
    unsigned maxRGB=0,maxA=0;
    for(unsigned iteration=0;iteration<35;iteration++){
        for(unsigned i=0;i<n;i++){
            p[i]=uint8_t(i*37+iteration*7);p[n+i]=uint8_t(i*53+iteration*13);
            p[2*n+i]=uint8_t(i*71+iteration*17);p[3*n+i]=uint8_t(i+iteration*3);
        }
        for(unsigned y=0;y<h;y++){
            host_video_gray_row(p.data()+3*n+y*w,alpha.data()+y*w,w);
            host_video_yuv444_row(p.data()+y*w,p.data()+n+y*w,p.data()+2*n+y*w,alpha.data()+y*w,ref.data()+4*y*w,w);
        }
        const uint8_t* input=p.data();
        if(iteration%4!=3){input=slots[iteration%4];std::copy(p.begin(),p.end(),slots[iteration%4]);}
        auto* next=direct::video_yuva_convert(gpu,w,h,input);if(!next){direct::log("FAIL conversion iteration=%u",iteration);return 3;}
        gpu=next;
        for(unsigned i=0;i<4*n;i++){
            unsigned d=unsigned(std::abs(int(gpu->pixels[i])-int(ref[i])));
            if(i%4==3)maxA=std::max(maxA,d);else maxRGB=std::max(maxRGB,d);
        }
        if(cpu){if(!direct::update(cpu,ref.data(),0,0,w,h))return 4;}
        else cpu=direct::texture(w,h,ref.data());
        if(!cpu)return 5;
        pair(cpu,gpu); // next iteration reuses resources after an actual draw
    }
    direct::log("YUVA_PROBE cpu_readback_rgb_max=%u alpha_max=%u frames=35 final_mapped_slot=2; emulator CPU readback requires independent screenshot oracle",maxRGB,maxA);
    direct::log("READY left=CPU right=GXM compare x=8 and 504 y=48 width=96 height=64 native-size");
    for(unsigned i=0;i<1800;i++)pair(cpu,gpu,true);
    direct::wait();direct::destroy(cpu);direct::destroy(gpu);direct::video_yuva_release();
    direct::video_yuva_queue_close();
    direct::prepare_process_exit();
    direct::log("DONE resources released and display queue drained");
    sceKernelExitProcess(0);return 0;
}
