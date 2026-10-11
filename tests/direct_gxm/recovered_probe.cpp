// Independent production-host packet probe. No game/save paths are touched.
#include "gpu.hpp"
#include "render_packet.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/power.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <cassert>
extern "C" {unsigned int _newlib_heap_size_user=64*1024*1024;}
namespace direct {void log(const char* format,...){FILE*f=fopen("ux0:data/art3m1s-recovered-probe/result.log","a");if(f){va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);fputc('\n',f);fclose(f);}}}
struct Lease {direct::Texture* texture;unsigned refs=1;};
static unsigned liveLeases=0,retiredInScene=0;static uint64_t identity=0;
static void retain(void*p){++static_cast<Lease*>(p)->refs;}
static void release(void*p){auto*l=static_cast<Lease*>(p);if(!--l->refs){if(direct::in_scene())++retiredInScene;direct::destroy(l->texture);delete l;--liveLeases;}}
static void* resolve(void*p){return static_cast<Lease*>(p)->texture;}
static Lease* upload(unsigned char r,unsigned char g,unsigned char b){unsigned char rgba[]={r,g,b,255};auto*t=direct::texture(1,1,rgba);if(!t)return nullptr;++liveLeases;return new Lease{t};}
static bool mesh(EmRenderPacket*p,Lease*l,float x0,float y0,float x1,float y1){
 EmRenderVertex v[]={{x0/480-1,1-y0/272,.5,1,1,1,1,1,0,0},{x1/480-1,1-y0/272,.5,1,1,1,1,1,1,0},{x0/480-1,1-y1/272,.5,1,1,1,1,1,0,1},{x1/480-1,1-y1/272,.5,1,1,1,1,1,1,1}};uint16_t ix[]={0,1,2,1,3,2};EmRenderLease lease{l,retain,release,resolve,{uint64_t(reinterpret_cast<uintptr_t>(l)),1,1}};return em_render_packet_mesh(p,&lease,v,4,ix,6)==0;
}
static EmRenderPacket* makePacket(bool soft,Lease* mask,Lease* color){auto*p=em_render_packet_create();if(!p)return nullptr;
 auto op=[&](EmRenderOp o,int a=0,int b=0,int c=0,int d=0){return em_render_packet_command(p,o,a,b,c,d)==0;};
 bool ok=op(EM_RENDER_MATERIAL,0);
 if(soft)ok=ok&&op(EM_RENDER_SOFT_BEGIN)&&op(EM_RENDER_SOFT_REGION,90,90,710,450)&&op(EM_RENDER_SOFT_POSITIVE);
 else ok=ok&&op(EM_RENDER_HARD_RESET,1)&&op(EM_RENDER_HARD_STEP,1,1);
 ok=ok&&mesh(p,mask,180,140,620,390);
 if(soft)ok=ok&&op(EM_RENDER_SOFT_NEGATIVE);else ok=ok&&op(EM_RENDER_HARD_STEP,2,2);
 ok=ok&&mesh(p,mask,320,220,470,310);
 if(soft)ok=ok&&op(EM_RENDER_SOFT_END)&&op(EM_RENDER_MATERIAL,0)&&op(EM_RENDER_MASK_STATE);else ok=ok&&op(EM_RENDER_MATERIAL,0)&&op(EM_RENDER_HARD_STEP,2,0);
 ok=ok&&mesh(p,color,100,100,700,440);
 ok=ok&&(soft?op(EM_RENDER_SOFT_DISABLE):op(EM_RENDER_MASK_STATE));
 if(!ok||!em_render_packet_seal(p)){em_render_packet_destroy(p);return nullptr;}return p;
}
static direct::EffectDraw composite(){direct::EffectDraw d{};d.transform[0]=d.transform[3]=1;d.quad[0]=960;d.quad[1]=544;d.uv[2]=d.uv[3]=1;for(float&v:d.tint)v=1;d.effects.flags[0]=2;d.effects.transition[2]=1;d.blend=5;return d;}
int main(){sceIoMkdir("ux0:data/art3m1s-recovered-probe",0777);sceIoRemove("ux0:data/art3m1s-recovered-probe/result.log");const char entered[]="main entered before frequency/init\n";int fd=sceIoOpen("ux0:data/art3m1s-recovered-probe/entered.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);if(fd>=0){sceIoWrite(fd,entered,sizeof entered-1);sceIoClose(fd);}direct::log("[probe-start] main entered heap=%u",_newlib_heap_size_user);int cpuRc=scePowerSetArmClockFrequency(444),gpuRc=scePowerSetGpuClockFrequency(222);direct::log("[probe-start] clocks cpu_rc=%08x gpu_rc=%08x; entering gpu init",unsigned(cpuRc),unsigned(gpuRc));
 if(!direct::init()){direct::log("FAIL init");sceKernelExitProcess(1);return 1;}
 std::vector<unsigned char> pixels(960*544*4);bool all=true;unsigned stage=0;
 const float transform[]={.85f,.1f,-.08f,.9f,50,15},clip[]={190.25f,140.75f,570.5f,370.25f};auto effect=composite();
 for(unsigned mapped=0;mapped<2;++mapped)for(unsigned soft=0;soft<2;++soft)for(unsigned depth=0;depth<3;++depth){
  auto*mask=upload(255,255,255);auto*color=upload(220,60,100);if(!mask||!color){all=false;break;}auto*p=makePacket(soft,mask,color);direct::Texture* textures[]={mask->texture,color->texture};release(mask);release(color);if(!p){all=false;break;}
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);bool ok=true;unsigned opened=0;for(;opened<depth;++opened)if(!direct::group_begin()){ok=false;break;}
  if(ok)ok=direct::draw_recovered_packet(p,textures,2,mapped?transform:nullptr,1,mapped?clip:nullptr);
  // Packet release is intentionally INSIDE the live parent scene. The lease's
  // final release calls host destroy(), which retires until the host end fence.
  em_render_packet_destroy(p);
  for(unsigned i=0;i<opened;++i)direct::group_end(effect,nullptr,1,1);
  direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();
  ok=direct::readback(960,544,pixels.data())&&ok;char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",stage);FILE*f=fopen(path,"wb");if(!f)ok=false;else {ok=(fwrite(pixels.data(),1,pixels.size(),f)==pixels.size())&&ok;fclose(f);}
  direct::log("stage=%u mapped=%u soft=%u depth=%u ok=%d leases=%u retired_in_scene=%u cpu=%d gpu=%d",stage,mapped,soft,depth,int(ok),liveLeases,retiredInScene,scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());all=all&&ok&&!liveLeases;++stage;
 }
 // Unsupported parent opacity must fail before packet submission, leaving the
 // parent scene usable. The ordinary marker exercises restored host bindings.
 auto*mask=upload(255,255,255);auto*color=upload(220,60,100);auto*p=makePacket(true,mask,color);direct::Texture* textures[]={mask->texture,color->texture};release(mask);release(color);
 direct::begin();direct::rect(0,0,960,544,0x141e28ff);bool failure=direct::group_begin();if(failure){failure=!direct::draw_recovered_packet(p,textures,2,nullptr,.5f,nullptr);direct::rect(300,200,100,100,0x20d050ff);direct::group_end(effect,nullptr,1,1);}em_render_packet_destroy(p);direct::end();direct::wait();failure=failure&&direct::readback(960,544,pixels.data());FILE*f=fopen("ux0:data/art3m1s-recovered-probe/stage-12.rgba","wb");if(f){failure=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&failure;fclose(f);}else failure=false;
 direct::log("unsupported-opacity-recovery=%d leases=%u retired_in_scene=%u",int(failure),liveLeases,retiredInScene);all=all&&failure&&!liveLeases;
 direct::prepare_process_exit();direct::log("COMPLETE ok=%d stages=13 GPU-drained=1",int(all));sceKernelExitProcess(all?0:2);return all?0:2;
}
