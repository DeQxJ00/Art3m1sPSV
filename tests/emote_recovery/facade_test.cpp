#include "emote_recovery_facade.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include <cstring>
#include <ctime>
#include <unordered_set>
extern "C" {
#include "render_packet.h"
#include "texture_host.h"
#include "gpu.h"
long long sceKernelGetSystemTimeWide(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000000+t.tv_nsec/1000;}
int gpu_texture(GpuTexture*,int,int,const uint32_t*){assert(false);return -1;}
int gpu_texture_bc3_linear(GpuTexture*,int,int,const void*,unsigned){assert(false);return -1;}
int gpu_texture_bc3_swizzled(GpuTexture*,int,int,const void*,unsigned){assert(false);return -1;}
void gpu_texture_free(GpuTexture*){assert(false);}
int gpu_mesh(GpuTexture*,const GpuVertex*,int,const uint16_t*,int){assert(false);return -1;}
int gpu_mask_state(unsigned,unsigned){assert(false);return -1;}
int gpu_mask_clear(){assert(false);return -1;}
}
static bool defer=true;static unsigned uploads=0,releases=0,draws=0;static uint64_t next_id=0;static std::unordered_set<uint64_t> live;
static int upload(void*,const EmTextureSource *s,uint64_t *out){assert(s->data&&s->bytes&&s->width&&s->height);if(defer){*out=0;return 0;}*out=++next_id;live.insert(*out);++uploads;return 1;}
static void release(void*,uint64_t id){assert(live.erase(id)==1);++releases;}
static const EmTextureHost host={nullptr,upload,release};
extern "C" const EmTextureHost *art3m1s_gxm_recovered_texture_host(){return &host;}
extern "C" int art3m1s_gxm_prepare_recovered_packet(const EmRenderPacket *p){std::vector<uint64_t> h(em_render_packet_texture_count(p));return em_render_packet_prepare_host(p,h.data(),h.size());}
extern "C" int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket *p,const float *affine,float opacity,const float*){assert(affine&&affine[0]==1&&opacity==1);uint32_t n=0;assert(em_render_packet_commands(p,&n)&&n);++draws;return 1;}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);auto len=ftell(f);rewind(f);std::vector<uint8_t>b(len);assert(fread(b.data(),1,b.size(),f)==b.size());fclose(f);
 assert(!art3m1s_recovery_create("bad",b.data(),8,960,544));
 uint64_t a=art3m1s_recovery_create("sample01-kog_12.psb",b.data(),b.size(),960,544);assert(a);uint64_t c=art3m1s_recovery_create("second",b.data(),b.size(),960,544);assert(c&&a!=c);
 float args[4]={.75f,0,0,0},value=0;assert(art3m1s_recovery_command(a,3,"face_talk",args,0));assert(art3m1s_recovery_query(a,1,"face_talk",&value)&&value==.75f);assert(art3m1s_recovery_query(c,1,"face_talk",&value)&&value==0);
 assert(!art3m1s_recovery_command(a,99,"",args,0));args[0]=.5f;assert(!art3m1s_recovery_command(a,9,"",args,0));
 assert(art3m1s_recovery_command(a,4,"はい",args,1));assert(art3m1s_recovery_query(a,2,"はい",&value)&&value==1);
 assert(art3m1s_recovery_advance(a,1));auto packet=art3m1s_recovery_capture(a);assert(packet&&uploads==0);assert(art3m1s_recovery_packet_prepare(packet)==0);
 art3m1s_recovery_destroy(a);assert(art3m1s_recovery_collect());assert(!art3m1s_recovery_query(a,1,"face_talk",&value));
 defer=false;assert(art3m1s_recovery_packet_prepare(packet)==1&&uploads);float affine[6]={1,0,0,1,0,0};assert(art3m1s_recovery_packet_draw(packet,affine,1,nullptr)==1&&draws==1);
 unsigned before=releases;std::thread other([&]{float v;assert(!art3m1s_recovery_query(c,1,"face_talk",&v));assert(art3m1s_recovery_packet_prepare(packet)==-1);art3m1s_recovery_packet_release(packet);art3m1s_recovery_destroy(c);assert(!art3m1s_recovery_collect());});other.join();assert(releases==before);
 assert(art3m1s_recovery_collect());assert(live.empty()&&releases==uploads);assert(art3m1s_recovery_packet_prepare(packet)==-1);assert(art3m1s_recovery_shutdown());
 std::thread next([&]{auto id=art3m1s_recovery_create("next-game",b.data(),b.size(),960,544);assert(id);auto p=art3m1s_recovery_capture(id);assert(p);assert(art3m1s_recovery_packet_prepare(p)==1);assert(art3m1s_recovery_shutdown());});next.join();assert(live.empty());
 puts("PASS real sample01 contexts/command/query/parallel gesture; capture survives model destruction; deferred upload retry; cross-thread release queued; shutdown/rebind new owner; no legacy GPU calls");}
