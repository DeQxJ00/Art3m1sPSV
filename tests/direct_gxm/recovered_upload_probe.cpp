// Independent production-host packet probe. No game/save paths are touched.
#include "gpu.hpp"
#include "render_packet.h"
#include "recovered_emote_bridge.hpp"
#include "bc3_layout.hpp"
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
struct Lease {EmTextureSource source{};std::vector<uint8_t> bytes;uint64_t token=0;unsigned refs=1;};
static unsigned liveLeases=0,retiredInScene=0,deferred=0,ready=0;static uint64_t identity=0;
static void retain(void*p){++static_cast<Lease*>(p)->refs;}
static void release(void*p){auto*l=static_cast<Lease*>(p);if(!--l->refs){if(l->token){if(direct::in_scene())++retiredInScene;const auto*h=direct::recovered_texture_host();h->release(h->user,l->token);}delete l;--liveLeases;}}
static int prepare(void*p,uint64_t*out){auto*l=static_cast<Lease*>(p);*out=l->token;if(l->token)return 1;const auto*h=direct::recovered_texture_host();int rc=h->upload(h->user,&l->source,&l->token);if(rc==1){*out=l->token;std::vector<uint8_t>().swap(l->bytes);l->source.data=nullptr;++ready;}else if(rc==0)++deferred;return rc;}
static Lease* source(unsigned w,unsigned h,unsigned format,unsigned layout,std::vector<uint8_t> bytes){auto*l=new Lease;l->bytes=std::move(bytes);l->source={++identity,format,layout,w,h,w,h,unsigned(l->bytes.size()),l->bytes.data()};++liveLeases;return l;}
static Lease* upload(unsigned char r,unsigned char g,unsigned char b){return source(1,1,EM_TEXTURE_RGBA8,EM_TEXTURE_LINEAR,{r,g,b,255});}
static bool prepareOutside(EmRenderPacket*p){direct::begin();int first=direct::prepare_recovered_packet(p);direct::end();direct::wait();int second=direct::prepare_recovered_packet(p);direct::log("[host-upload] first_in_scene=%d retry_outside=%d deferred=%u ready=%u",first,second,deferred,ready);return first==0&&second==1;}
static bool mesh(EmRenderPacket*p,Lease*l,float x0,float y0,float x1,float y1){
 EmRenderVertex v[]={{x0/480-1,1-y0/272,.5,1,1,1,1,1,0,0},{x1/480-1,1-y0/272,.5,1,1,1,1,1,1,0},{x0/480-1,1-y1/272,.5,1,1,1,1,1,0,1},{x1/480-1,1-y1/272,.5,1,1,1,1,1,1,1}};uint16_t ix[]={0,1,2,1,3,2};EmRenderLease lease{l,retain,release,nullptr,{l->source.identity,l->source.width,l->source.height},prepare};return em_render_packet_mesh(p,&lease,v,4,ix,6)==0;
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
 if(!direct::initialize_release_render_paths()){direct::log("FAIL release render paths");return 3;}
 std::vector<unsigned char> pixels(960*544*4);bool all=true;unsigned stage=0;
 const float transform[]={.85f,.1f,-.08f,.9f,50,15},clip[]={190.25f,140.75f,570.5f,370.25f};auto effect=composite();
 for(unsigned mapped=0;mapped<2;++mapped)for(unsigned soft=0;soft<2;++soft)for(unsigned depth=0;depth<3;++depth){
  auto*mask=upload(255,255,255);auto*color=upload(220,60,100);if(!mask||!color){all=false;break;}auto*p=makePacket(soft,mask,color);release(mask);release(color);if(!p||!prepareOutside(p)){all=false;break;}
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);bool ok=true;unsigned opened=0;for(;opened<depth;++opened)if(!direct::group_begin()){ok=false;break;}
  if(ok)ok=direct::replay_recovered_packet(p,mapped?transform:nullptr,1,mapped?clip:nullptr)==1;
  // Packet release is intentionally INSIDE the live parent scene. The lease's
  // final release calls host destroy(), which retires until the host end fence.
  em_render_packet_destroy(p);
  for(unsigned i=0;i<opened;++i)direct::group_end(effect,nullptr,1,1);
  direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();
  ok=direct::readback(960,544,pixels.data())&&ok;char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",stage);FILE*f=fopen(path,"wb");if(!f)ok=false;else {ok=(fwrite(pixels.data(),1,pixels.size(),f)==pixels.size())&&ok;fclose(f);}
  direct::log("stage=%u mapped=%u soft=%u depth=%u ok=%d leases=%u retired_in_scene=%u cpu=%d gpu=%d",stage,mapped,soft,depth,int(ok),liveLeases,retiredInScene,scePowerGetArmClockFrequency(),scePowerGetGpuClockFrequency());all=all&&ok&&!liveLeases;++stage;
 }
 // Invalid parent opacity must fail before packet submission, leaving the
 // parent scene usable. The ordinary marker exercises restored host bindings.
 auto*mask=upload(255,255,255);auto*color=upload(220,60,100);auto*p=makePacket(true,mask,color);release(mask);release(color);if(!p||!prepareOutside(p))return 4;
 direct::begin();direct::rect(0,0,960,544,0x141e28ff);bool failure=direct::group_begin();if(failure){failure=direct::replay_recovered_packet(p,nullptr,-.5f,nullptr)<0;direct::rect(300,200,100,100,0x20d050ff);direct::group_end(effect,nullptr,1,1);}em_render_packet_destroy(p);direct::end();direct::wait();failure=failure&&direct::readback(960,544,pixels.data());FILE*f=fopen("ux0:data/art3m1s-recovered-probe/stage-12.rgba","wb");if(f){failure=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&failure;fclose(f);}else failure=false;
 direct::log("unsupported-opacity-recovery=%d leases=%u retired_in_scene=%u",int(failure),liveLeases,retiredInScene);all=all&&failure&&!liveLeases;

 // Identical source pattern in genuine linear and Morton-swizzled BC3 blocks.
 std::vector<uint8_t> linear(4*4*16,0);const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
 for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){auto*b=linear.data()+(y*4+x)*16;b[0]=b[1]=255;uint16_t c=colors[x];b[8]=b[10]=uint8_t(c);b[9]=b[11]=uint8_t(c>>8);}
 for(unsigned sw=0;sw<2;++sw){std::vector<uint8_t> bytes=linear;if(sw&&!direct::bc3_swizzle(bytes.data(),bytes.size(),linear.data(),linear.size(),16,16))return 5;
  auto*l=source(16,16,EM_TEXTURE_BC3,sw?EM_TEXTURE_SWIZZLED:EM_TEXTURE_LINEAR,std::move(bytes));auto*p=em_render_packet_create();bool ok=p&&em_render_packet_command(p,EM_RENDER_MATERIAL,-1,0,0,0)==0&&em_render_packet_command(p,EM_RENDER_MASK_STATE,0,0,0,0)==0&&mesh(p,l,250,140,650,440)&&em_render_packet_seal(p);release(l);ok=ok&&prepareOutside(p);
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);ok=direct::replay_recovered_packet(p,nullptr,1,nullptr)==1&&ok;em_render_packet_destroy(p);direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();ok=direct::readback(960,544,pixels.data())&&ok;char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",13+sw);FILE*f=fopen(path,"wb");if(f){ok=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&ok;fclose(f);}else ok=false;
  direct::log("stage=%u BC3 swizzled=%u ok=%d leases=%u deferred=%u ready=%u retired_in_scene=%u",13+sw,sw,int(ok),liveLeases,deferred,ready,retiredInScene);all=all&&ok&&!liveLeases;
 }

 // Opaque parent variants isolate RT preservation from extra UNORM roundoff
 // introduced by rendering soft edges over transparent black then compositing.
 for(unsigned mapped=0;mapped<2;++mapped)for(unsigned depth=0;depth<3;++depth){
  auto*mask=upload(255,255,255);auto*color=upload(220,60,100);auto*p=makePacket(true,mask,color);release(mask);release(color);bool ok=p&&prepareOutside(p);
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);unsigned opened=0;for(;opened<depth;++opened){if(!direct::group_begin()){ok=false;break;}direct::rect(0,0,960,544,0x141e28ff);}
  ok=direct::replay_recovered_packet(p,mapped?transform:nullptr,1,mapped?clip:nullptr)==1&&ok;em_render_packet_destroy(p);for(unsigned i=0;i<opened;++i)direct::group_end(effect,nullptr,1,1);direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();ok=direct::readback(960,544,pixels.data())&&ok;
  unsigned current=15+mapped*3+depth;char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",current);FILE*f=fopen(path,"wb");if(f){ok=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&ok;fclose(f);}else ok=false;
  direct::log("stage=%u opaque_parent=1 mapped=%u depth=%u ok=%d leases=%u deferred=%u ready=%u retired_in_scene=%u",current,mapped,depth,int(ok),liveLeases,deferred,ready,retiredInScene);all=all&&ok&&!liveLeases;
 }
 // Opacity must equal one composite of the fully rendered overlapping source.
 for(unsigned variant=0;variant<6;++variant){
  auto*mask=upload(255,255,255);auto*color=upload(220,60,100);auto*p=makePacket(true,mask,color);release(mask);release(color);bool ok=p&&prepareOutside(p);
  float alpha=variant/2==0?0.f:variant/2==1?.5f:1.f;
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);
  if(variant%2==0)ok=direct::replay_recovered_packet(p,nullptr,alpha,nullptr)==1&&ok;
  else {ok=direct::group_begin()&&ok;ok=direct::replay_recovered_packet(p,nullptr,1,nullptr)==1&&ok;auto d=composite();d.effects.flags[0]=3;d.effects.transition[2]=0;d.tint[3]=alpha;direct::group_end(d,nullptr,1,1);}
  em_render_packet_destroy(p);direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();ok=direct::readback(960,544,pixels.data())&&ok;
  char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",21+variant);FILE*f=fopen(path,"wb");if(f){ok=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&ok;fclose(f);}else ok=false;
  direct::log("stage=%u opacity=%.3f reference=%u ok=%d leases=%u",21+variant,alpha,variant%2,int(ok),liveLeases);all=all&&ok&&!liveLeases;
 }
 // Consecutive packet reuse must not inherit the previous packet's clip/stencil.
 for(unsigned isolated=0;isolated<2;++isolated){
  auto*mask=upload(255,255,255);auto*color=upload(220,60,100);auto*p=makePacket(false,mask,color);release(mask);release(color);bool ok=p&&prepareOutside(p);
  const float left[]={.5f,0,0,.5f,0,0},right[]={.5f,0,0,.5f,430,240},leftClip[]={120,90,200,160};
  direct::begin();direct::rect(0,0,960,544,0x141e28ff);
  ok=direct::replay_recovered_packet(p,left,1,leftClip)==1&&ok;
  if(isolated)ok=direct::group_begin()&&ok;
  ok=direct::replay_recovered_packet(p,right,1,nullptr)==1&&ok;
  if(isolated)direct::group_end(effect,nullptr,1,1);
  em_render_packet_destroy(p);direct::rect(850,450,60,60,0x20d050ff);direct::end();direct::wait();ok=direct::readback(960,544,pixels.data())&&ok;
  char path[128];snprintf(path,sizeof path,"ux0:data/art3m1s-recovered-probe/stage-%02u.rgba",27+isolated);FILE*f=fopen(path,"wb");if(f){ok=fwrite(pixels.data(),1,pixels.size(),f)==pixels.size()&&ok;fclose(f);}else ok=false;
  direct::log("stage=%u consecutive-packets=2 isolated=%u ok=%d leases=%u",27+isolated,isolated,int(ok),liveLeases);all=all&&ok&&!liveLeases;
 }
 direct::prepare_process_exit();direct::log("COMPLETE ok=%d stages=29 GPU-drained=1 real-host-callbacks=1 deferred=%u ready=%u retired_in_scene=%u",int(all),deferred,ready,retiredInScene);sceKernelExitProcess(all?0:2);return all?0:2;
}
