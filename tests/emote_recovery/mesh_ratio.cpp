// Real evaluator/packet regression; host texture callbacks, no GPU context.
#include "emote_recovery_facade.cpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <ctime>
extern "C" {
#include "gpu.h"
int gpu_texture(GpuTexture*,int,int,const uint32_t*){std::abort();}
int gpu_texture_bc3_linear(GpuTexture*,int,int,const void*,unsigned){std::abort();}
int gpu_texture_bc3_swizzled(GpuTexture*,int,int,const void*,unsigned){std::abort();}
void gpu_texture_free(GpuTexture*){std::abort();}
int gpu_mesh(GpuTexture*,const GpuVertex*,int,const uint16_t*,int){std::abort();}
int gpu_grid(GpuTexture*,const GpuVertex*,int){std::abort();}
int gpu_material_mode(unsigned){std::abort();}
int gpu_mask_state(unsigned,unsigned){std::abort();}
int gpu_mask_clear(){std::abort();}
int gpu_hard_mask_reset(unsigned){std::abort();}
int gpu_hard_mask_step(unsigned,unsigned){std::abort();}
int gpu_soft_mask_begin(){std::abort();}
int gpu_soft_mask_region(int,int,int,int){std::abort();}
int gpu_soft_mask_positive(){std::abort();}
int gpu_soft_mask_negative(){std::abort();}
int gpu_soft_mask_end(){std::abort();}
int gpu_soft_mask_disable(){std::abort();}
}
namespace direct {void log(const char *,...) {}}
extern "C" int64_t sceKernelGetSystemTimeWide(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return int64_t(t.tv_sec)*1000000+t.tv_nsec/1000;}
static unsigned uploads=0,releases=0;
static int upload(void*,const EmTextureSource*,uint64_t *out){*out=++uploads;return 1;}
static void release(void*,uint64_t){++releases;}
extern "C" const EmTextureHost *art3m1s_gxm_recovered_texture_host(){static EmTextureHost h{nullptr,upload,release};return &h;}
extern "C" int art3m1s_gxm_prepare_recovered_packet(const EmRenderPacket*p){std::vector<uint64_t> h(em_render_packet_texture_count(p));return em_render_packet_prepare_host(p,h.data(),h.size());}
extern "C" int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket*,const float*,float,const float*){std::abort();}
static std::vector<EmRenderVertex> vertices(uint64_t token){
 uint32_t n=0;auto *v=em_render_packet_vertices(packets.at(token).packet,&n);assert(n&&v);
 uint32_t count=0;auto *ix=em_render_packet_indices(packets.at(token).packet,&count);assert(ix&&count);
 auto *commands=em_render_packet_commands(packets.at(token).packet,&count);assert(commands&&count);
 return {v,v+n};
}
static void same(const std::vector<EmRenderVertex>&a,const std::vector<EmRenderVertex>&b){assert(a.size()==b.size()&&!memcmp(a.data(),b.data(),a.size()*sizeof(a[0])));}
static void ratio(uint64_t id,float r){float args[4]={r,0,0,0};assert(art3m1s_recovery_command(id,9,"",args,0));float out=0;assert(art3m1s_recovery_query(id,3,"",&out)&&out==r);}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);assert(f);std::vector<uint8_t>b((std::istreambuf_iterator<char>(f)),{});
 for(bool deformed:{false,true})for(unsigned quality:{1u,2u,4u,6u,8u}){
  assert(art3m1s_recovery_set_grid_steps(quality));size_t bytes=0;auto model=art3m1s_recovery_model_prepare(b.data(),b.size(),0,&bytes);assert(model);
  // Also exercise a known curved fixture: a real model's neutral pose may
  // contain only quads, which alone cannot prove that subdivision changed.
  if(deformed){auto &scene=cpu_models.at(model)->scene;
   for(unsigned m=0;m<scene.motion_count;++m)for(unsigned n=0;n<scene.motions[m].layer_count;++n){auto &l=scene.motions[m].layers[n];
    if(l.type!=0||l.combinator_count)continue;l.mesh_transform=1;
    for(unsigned k=0;k<l.frame_count;++k){auto &c=l.frames[k].content;if(c.w<=0||c.h<=0)continue;
     c.has_patch=1;c.width=c.w;c.height=c.h;
     for(int j=0;j<16;++j){float x=(j%4)/3.f,y=(j/4)/3.f;c.cp[j]={x+.1f*y*y,y};}
    }
   }
  }
  auto a=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);
  auto c=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);assert(a&&c);art3m1s_recovery_model_release(model);
  for(auto id:{a,c}){Selection s(*find(id));model_controls_seed(1);timeline_seed_idle(1);timeline_select(0);timeline_seek(12);timeline_step(20);model_controls_step(20);model_view_motion_step(20);}
  auto before=art3m1s_recovery_capture(a);auto original=vertices(before);const auto other=art3m1s_recovery_capture(c);auto independent=vertices(other);
  for(float r:{.2f,.25f,.4f,.5f,.6f,.8f,1.f,.0001f,1.f}){
   // A write must join pending evaluation before changing its mesh cache.
   uint64_t ids[2]={a,c},pending[2]{};assert(art3m1s_recovery_capture_begin(ids,pending,2));
   ratio(a,r);assert(!pending_capture);for(auto p:pending)art3m1s_recovery_packet_release(p);
   {Selection s(*find(a));assert(model_view_evaluate(960,544,-1,-1));}
   auto p=art3m1s_recovery_capture(a);auto actual=vertices(p);
   const unsigned desired=unsigned(quality*r);unsigned expected=1;
   for(unsigned candidate:{1u,2u,4u,6u,8u})if(candidate>=desired){expected=candidate;break;}
   // Compare with an independently configured reference at the same pose.
   {Selection s(*find(c));assert(model_view_set_grid_steps(expected));}
   auto ref=art3m1s_recovery_capture(c);same(actual,vertices(ref));
   {Selection s(*find(c));assert(model_view_set_grid_steps(quality));}
   auto restored=art3m1s_recovery_capture(c);same(independent,vertices(restored));
   float out=0;assert(art3m1s_recovery_query(c,3,"",&out)&&out==1);
   if(r==1)same(original,actual);if(deformed&&quality>1&&r==.2f)assert(actual.size()<original.size());
   same(original,vertices(before)); // retained old topology remains immutable
   for(auto q:{p,ref,restored})art3m1s_recovery_packet_release(q);
  }
  for(float invalid:{0.f,-.1f,1.1f,INFINITY,NAN}){
   float args[4]={invalid,0,0,0};assert(!art3m1s_recovery_command(a,9,"",args,0));
   float out=0;assert(art3m1s_recovery_query(a,3,"",&out)&&out==1);
   auto p=art3m1s_recovery_capture(a);same(original,vertices(p));art3m1s_recovery_packet_release(p);
  }
  assert(art3m1s_recovery_advance(a,1));float args[4]={};assert(art3m1s_recovery_command(a,8,"",args,0));
  art3m1s_recovery_packet_release(before);art3m1s_recovery_packet_release(other);assert(art3m1s_recovery_shutdown());
 }
 assert(uploads==releases);puts("PASS: ratios, actual mesh topology, per-instance isolation, pending evaluation join, retained packets, cache restoration, invalid values, progress/pass");
}
