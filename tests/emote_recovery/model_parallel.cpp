// Actual facade and adapter; fake owner-only host texture API, no GPU pixels.
#include "emote_recovery_facade.cpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstdio>
namespace direct {void log(const char *,...) {}}
static std::thread::id test_owner=std::this_thread::get_id();
static uint64_t upload_count=0,release_count=0;
static int upload(void *,const EmTextureSource *,uint64_t *id){assert(std::this_thread::get_id()==test_owner);*id=++upload_count;return 1;}
static void release(void *,uint64_t){assert(std::this_thread::get_id()==test_owner);++release_count;}
extern "C" const EmTextureHost *art3m1s_gxm_recovered_texture_host(){static EmTextureHost h{nullptr,upload,release};return &h;}
extern "C" int art3m1s_gxm_prepare_recovered_packet(const EmRenderPacket *p){assert(std::this_thread::get_id()==test_owner);uint64_t handles[64];return em_render_packet_prepare_host(p,handles,64);}
extern "C" int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket *,const float *,float,const float *){assert(false);return -1;}
static unsigned checks=0;
static void same(uint64_t a,uint64_t b){assert(a&&b);auto *p=packets.at(a).packet,*q=packets.at(b).packet;uint32_t n,m;
 auto *x=em_render_packet_commands(p,&n),*y=em_render_packet_commands(q,&m);assert(n==m&&!memcmp(x,y,n*sizeof(*x)));
 auto *v=em_render_packet_vertices(p,&n),*w=em_render_packet_vertices(q,&m);assert(n==m&&!memcmp(v,w,n*sizeof(*v)));
 auto *i=em_render_packet_indices(p,&n),*j=em_render_packet_indices(q,&m);assert(n==m&&!memcmp(i,j,n*sizeof(*i)));
 assert(em_render_packet_texture_count(p)==em_render_packet_texture_count(q));
 for(unsigned k=0;k<em_render_packet_texture_count(p);++k){EmRenderTextureInfo u{},z{};assert(em_render_packet_texture(p,k,&u)&&em_render_packet_texture(q,k,&z));assert(u.identity==z.identity&&u.width==z.width&&u.height==z.height);}++checks;
}
static void step(uint64_t id,int frame){auto *instance=find(id);Selection s(*instance);if(frame%20==0)timeline_select(frame/20%timeline_count());timeline_seek(frame%20*3.25f);model_controls_step(.5f);model_view_motion_step(.5f);}
int main(int argc,char **argv){assert(argc==4);
 for(int round=0;round<3;++round){uint64_t serial[3],parallel[3];
  for(int n=0;n<3;++n){std::ifstream f(argv[n+1],std::ios::binary);assert(f);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});size_t retained=0;uint64_t model=art3m1s_recovery_model_prepare(bytes.data(),bytes.size(),0,&retained);assert(model&&retained);
   serial[n]=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);parallel[n]=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);assert(serial[n]&&parallel[n]);art3m1s_recovery_model_release(model);
  }
  uint64_t bad[2]={parallel[0],parallel[0]},out[3]={1,1,1};art3m1s_recovery_capture_batch(bad,out,2);assert(!out[0]&&!out[1]);
  assert(!art3m1s_recovery_capture_begin(bad,out,2)&&!out[0]&&!out[1]);
  uint64_t unknown=UINT64_MAX;art3m1s_recovery_capture_batch(&unknown,out,1);assert(!out[0]);
  for(int frame=0;frame<120;++frame){uint64_t expected[3],ids[3];for(int n=0;n<3;++n){step(serial[n],frame);step(parallel[n],frame);expected[n]=art3m1s_recovery_capture(serial[n]);ids[n]=parallel[(n+frame)%3];}
   if(frame%4==0){art3m1s_recovery_capture_batch(ids,out,2);art3m1s_recovery_capture_batch(ids+2,out+2,1);}
   else if(frame%4==1){uint64_t mixed[4]={ids[0],UINT64_MAX,ids[1],ids[2]},captured[4];art3m1s_recovery_capture_batch(mixed,captured,4);assert(!captured[1]);out[0]=captured[0];out[1]=captured[2];out[2]=captured[3];}
   else if(frame%4==2){if(!art3m1s_recovery_capture_begin(ids,out,2))art3m1s_recovery_capture_batch(ids,out,2);
    // Reading an unfinished packet joins safely; explicit finish is idempotent.
    if(frame%8==2){std::vector<unsigned> host_work(1024,17);for(auto &value:host_work)value=value*3+1;assert(host_work.back()==52);art3m1s_recovery_capture_finish();}
    assert(art3m1s_recovery_packet_prepare(out[0])==1);art3m1s_recovery_capture_finish();art3m1s_recovery_capture_finish();art3m1s_recovery_capture_batch(ids+2,out+2,1);}
   else art3m1s_recovery_capture_batch(ids,out,3);
   for(int n=0;n<3;++n){same(expected[(n+frame)%3],out[n]);assert(art3m1s_recovery_packet_prepare(out[n])==1);}
#ifdef EM_TEST_EVAL_FAIL
   assert(!evaluation_pool.ready&&!evaluation_pool.workers[0].started&&!evaluation_pool.workers[1].started);
#else
   assert(evaluation_pool.ready&&evaluation_pool.workers[0].started&&evaluation_pool.workers[1].started);
#endif
   for(int n=0;n<3;++n){art3m1s_recovery_packet_release(expected[n]);art3m1s_recovery_packet_release(out[n]);}
  }
  // Step/Skip must join pending evaluation before changing player controls.
  for(unsigned op=10;op<=11;++op){float args[4]={5,20,0,0},value=0;uint64_t held[2];
   assert(art3m1s_recovery_command(serial[0],3,"test_finish",args,0));assert(art3m1s_recovery_command(parallel[0],3,"test_finish",args,0));
   if(!art3m1s_recovery_capture_begin(parallel,held,2))art3m1s_recovery_capture_batch(parallel,held,2);
   args[0]=args[1]=0;assert(art3m1s_recovery_command(parallel[0],op,"",args,0));assert(!pending_capture);
   assert(art3m1s_recovery_command(serial[0],op,"",args,0));assert(art3m1s_recovery_query(parallel[0],1,"test_finish",&value)&&value==5);
   auto a=art3m1s_recovery_capture(serial[0]),b=art3m1s_recovery_capture(parallel[0]);same(a,b);
   art3m1s_recovery_packet_release(a);art3m1s_recovery_packet_release(b);for(auto packet:held)art3m1s_recovery_packet_release(packet);
  }
  uint64_t last[3];art3m1s_recovery_capture_batch(parallel+2,last+2,1);
  if(!art3m1s_recovery_capture_begin(parallel,last,2))art3m1s_recovery_capture_batch(parallel,last,2);
  if(round==2){assert(art3m1s_recovery_shutdown());assert(!pending_capture&&upload_count==release_count&&instances.empty()&&packets.empty());continue;}
  // Retirement from another thread cannot destroy views used by pending jobs.
  std::thread retire([&]{art3m1s_recovery_destroy(parallel[0]);});retire.join();
  for(int n=0;n<3;++n){art3m1s_recovery_destroy(serial[n]);art3m1s_recovery_destroy(parallel[n]);}assert(art3m1s_recovery_collect());
  for(auto packet:last){assert(art3m1s_recovery_packet_prepare(packet)==1);art3m1s_recovery_packet_release(packet);}
  assert(art3m1s_recovery_shutdown());assert(upload_count==release_count&&instances.empty()&&packets.empty());
 }
 printf("PASS parallel packet comparisons=%u reload/shutdown=3 owner-only resources=%llu/%llu\n",checks,(unsigned long long)upload_count,(unsigned long long)release_count);
}
