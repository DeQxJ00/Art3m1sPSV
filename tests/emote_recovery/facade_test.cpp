#include "emote_recovery_facade.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include <cstring>
#include <ctime>
#include <cmath>
#include <unordered_set>
#include <unordered_map>
namespace direct {void log(const char *,...) {}}
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
static std::vector<EmRenderVertex> observed;
static bool defer=true,fail_upload=false;static unsigned uploads=0,releases=0,draws=0;static uint64_t next_id=0;static std::unordered_set<uint64_t> live;
static std::unordered_map<uint64_t,uint64_t> source_hashes;
static int upload(void*,const EmTextureSource *s,uint64_t *out){assert(s->data&&s->bytes&&s->width&&s->height);
 uint64_t hash=1469598103934665603ULL;for(unsigned j=0;j<s->bytes;++j)hash=(hash^s->data[j])*1099511628211ULL;
 auto saved=source_hashes.emplace(s->identity,hash);assert(saved.second||saved.first->second==hash);
 if(fail_upload){*out=0;return -1;}if(defer){*out=0;return 0;}*out=++next_id;live.insert(*out);++uploads;return 1;}
static void release(void*,uint64_t id){assert(live.erase(id)==1);++releases;}
static const EmTextureHost host={nullptr,upload,release};
extern "C" const EmTextureHost *art3m1s_gxm_recovered_texture_host(){return &host;}
extern "C" int art3m1s_gxm_prepare_recovered_packet(const EmRenderPacket *p){std::vector<uint64_t> h(em_render_packet_texture_count(p));return em_render_packet_prepare_host(p,h.data(),h.size());}
extern "C" int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket *p,const float *affine,float opacity,const float*){assert(affine&&affine[0]==1&&opacity==1);uint32_t n=0;assert(em_render_packet_commands(p,&n)&&n);uint32_t nv=0;auto v=em_render_packet_vertices(p,&nv);observed.assign(v,v+nv);++draws;return 1;}
int main(int argc,char **argv){assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);auto len=ftell(f);rewind(f);std::vector<uint8_t>b(len);assert(fread(b.data(),1,b.size(),f)==b.size());fclose(f);
 assert(!art3m1s_recovery_create("bad",b.data(),8,960,544));
 uint64_t a=art3m1s_recovery_create("sample01-kog_12.psb",b.data(),b.size(),960,544);assert(a);uint64_t c=art3m1s_recovery_create("second",b.data(),b.size(),960,544);assert(c&&a!=c);
 float args[4]={.75f,0,0,0},value=0;assert(art3m1s_recovery_command(a,3,"face_talk",args,0));assert(art3m1s_recovery_query(a,1,"face_talk",&value)&&value==.75f);assert(art3m1s_recovery_query(c,1,"face_talk",&value)&&value==0);
 assert(!art3m1s_recovery_command(a,99,"",args,0));args[0]=.5f;assert(!art3m1s_recovery_command(a,9,"",args,0));
 assert(art3m1s_recovery_command(a,4,"はい",args,1));assert(art3m1s_recovery_query(a,2,"はい",&value)&&value==1);
 assert(art3m1s_recovery_advance(a,1));auto packet=art3m1s_recovery_capture(a);assert(packet&&uploads==0);assert(art3m1s_recovery_packet_prepare(packet)==0);
 art3m1s_recovery_destroy(a);assert(art3m1s_recovery_collect());assert(!art3m1s_recovery_query(a,1,"face_talk",&value));
 assert(art3m1s_recovery_prepare_resources()==0&&uploads==0);
 defer=false;assert(art3m1s_recovery_prepare_resources()==1&&uploads);unsigned prepared=uploads;assert(art3m1s_recovery_prepare_resources()==1&&uploads==prepared);assert(art3m1s_recovery_packet_prepare(packet)==1&&uploads==prepared);float affine[6]={1,0,0,1,0,0};assert(art3m1s_recovery_packet_draw(packet,affine,1,nullptr)==1&&draws==1);
 unsigned before=releases;std::thread other([&]{float v;assert(!art3m1s_recovery_query(c,1,"face_talk",&v));assert(art3m1s_recovery_packet_prepare(packet)==-1);art3m1s_recovery_packet_release(packet);art3m1s_recovery_destroy(c);assert(!art3m1s_recovery_collect());assert(art3m1s_recovery_prepare_resources()==-1);});other.join();assert(releases==before);
 assert(art3m1s_recovery_collect());assert(live.empty()&&releases==uploads);assert(art3m1s_recovery_packet_prepare(packet)==-1);assert(art3m1s_recovery_shutdown());
 std::thread next([&]{auto id=art3m1s_recovery_create("next-game",b.data(),b.size(),960,544);assert(id);auto p=art3m1s_recovery_capture(id);assert(p);assert(art3m1s_recovery_packet_prepare(p)==1);assert(art3m1s_recovery_shutdown());});next.join();assert(live.empty());
 auto small=art3m1s_recovery_create("small",b.data(),b.size(),960,544);
 auto large=art3m1s_recovery_create("large",b.data(),b.size(),1920,1088);assert(small&&large);
 assert(!art3m1s_recovery_create("empty",b.data(),b.size(),0,544));
 auto sp=art3m1s_recovery_capture(small),lp=art3m1s_recovery_capture(large);assert(sp&&lp);
 assert(art3m1s_recovery_packet_draw(sp,affine,1,nullptr));auto sv=observed;
 assert(art3m1s_recovery_packet_draw(lp,affine,1,nullptr));assert(sv.size()==observed.size()&&!sv.empty());
 for(size_t n=0;n<sv.size();++n){assert(std::fabs(observed[n].x-sv[n].x-1.f)<.00001f);assert(std::fabs(observed[n].y-sv[n].y+1.f)<.00001f);assert(observed[n].u==sv[n].u&&observed[n].v==sv[n].v);}
 assert(art3m1s_recovery_shutdown());assert(live.empty());
 // CPU preparation must work on a worker and must not upload textures.
 uint64_t model=0;size_t retained=0;unsigned upload_start=uploads;
 std::thread compiler([&]{size_t rejected=99;assert(!art3m1s_recovery_model_prepare(b.data(),b.size(),b.size()+1,&rejected)&&rejected==0);
  for(size_t extra:{size_t(8192),size_t(65536),size_t(262144)}){auto limited=art3m1s_recovery_model_prepare(b.data(),b.size(),b.size()+extra,&rejected);if(limited){assert(rejected<=b.size()+extra);art3m1s_recovery_model_release(limited);}}
  auto owned=art3m1s_recovery_model_prepare(b.data(),b.size(),0,&retained);assert(owned&&retained>b.size());art3m1s_recovery_model_release(owned);
  model=art3m1s_recovery_model_prepare_shared(b.data(),b.size(),8*1024*1024,&retained);});compiler.join();assert(model&&retained<b.size()&&uploads==upload_start);
 unsigned leases=0;auto drop_lease=+[](void *p){++*static_cast<unsigned*>(p);};
 assert(!art3m1s_recovery_create_prepared("bad",model,0,544,&leases,drop_lease)&&leases==0);
 auto original=art3m1s_recovery_create("reference",b.data(),b.size(),960,544);
 auto clone=art3m1s_recovery_create_prepared("clone",model,960,544,&leases,drop_lease);
 auto independent=art3m1s_recovery_create_prepared("independent",model,960,544,&leases,drop_lease);assert(original&&clone&&independent);
 art3m1s_recovery_model_release(model); // Instances must survive cache eviction.
 args[0]=.75f;for(auto id:{original,clone}){assert(art3m1s_recovery_command(id,3,"face_talk",args,0));assert(art3m1s_recovery_command(id,4,"はい",args,1));}
 assert(art3m1s_recovery_query(independent,1,"face_talk",&value)&&value==0);
 for(unsigned step=0;step<12;++step){
  assert(art3m1s_recovery_advance(independent,2));auto ip=art3m1s_recovery_capture(independent);assert(ip);art3m1s_recovery_packet_release(ip);
  for(auto id:{original,clone})assert(art3m1s_recovery_advance(id,1));
  auto p1=art3m1s_recovery_capture(original),p2=art3m1s_recovery_capture(clone);assert(p1&&p2);
  assert(art3m1s_recovery_packet_draw(p1,affine,1,nullptr));auto expected=observed;
  assert(art3m1s_recovery_packet_draw(p2,affine,1,nullptr));assert(expected.size()==observed.size()&&!observed.empty());
  assert(!std::memcmp(expected.data(),observed.data(),expected.size()*sizeof(EmRenderVertex)));
  art3m1s_recovery_packet_release(p1);art3m1s_recovery_packet_release(p2);
 }
 std::thread retire([&]{art3m1s_recovery_destroy(clone);art3m1s_recovery_destroy(independent);});retire.join();assert(leases==0);
 assert(art3m1s_recovery_collect()&&leases==2);assert(art3m1s_recovery_shutdown()&&live.empty());
 // Real backing is freed by the transferred lease. Deferred textures must
 // retain it past registry eviction and off-thread instance retirement.
 struct Backing {std::vector<uint8_t> bytes;unsigned drops=0;};
 auto drop_backing=+[](void *p){auto *b=static_cast<Backing*>(p);++b->drops;std::vector<uint8_t>().swap(b->bytes);};
 for(int outcome=0;outcome<3;++outcome){
  Backing backing{b};defer=true;fail_upload=false;
  auto m=art3m1s_recovery_model_prepare_shared(backing.bytes.data(),backing.bytes.size(),8*1024*1024,&retained);assert(m);
  auto instance=art3m1s_recovery_create_prepared("borrowed",m,960,544,&backing,drop_backing);assert(instance);
  auto p=art3m1s_recovery_capture(instance);assert(p&&art3m1s_recovery_packet_prepare(p)==0);
  art3m1s_recovery_model_release(m);
  std::thread off([&]{art3m1s_recovery_destroy(instance);});off.join();assert(art3m1s_recovery_collect());
  assert(!backing.drops&&!backing.bytes.empty());
  if(outcome){defer=false;fail_upload=outcome==2;assert(art3m1s_recovery_packet_prepare(p)==(fail_upload?-1:1));
   assert(backing.drops==(fail_upload?0u:1u));}
  art3m1s_recovery_packet_release(p);assert(art3m1s_recovery_collect());assert(backing.drops==1&&backing.bytes.empty());
  assert(art3m1s_recovery_shutdown()&&live.empty());
 }
 puts("PASS worker preparation / bounded admission / prototype eviction / deferred lease release / 12-frame bit-identical vertices and independent playback; borrowed backing survives registry+view destruction, byte-exact deferred uploads, cancel/failure/ready release exactly once; real sample01 contexts/command/query/parallel gesture; capture survives model destruction; deferred upload retry; cross-thread release queued; shutdown/rebind new owner; no legacy GPU calls; logical1920x1088 canvas moves center without changing UV/density");}
