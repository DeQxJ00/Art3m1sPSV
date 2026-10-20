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
 for(int round=0;round<5;++round){unsigned quality=(unsigned[]){8,6,4,2,1}[round];assert(art3m1s_recovery_set_grid_steps(quality));assert(!art3m1s_recovery_set_grid_steps(5));uint64_t serial[3],parallel[3];
  for(int n=0;n<3;++n){std::ifstream f(argv[n+1],std::ios::binary);assert(f);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});size_t retained=0;uint64_t model=art3m1s_recovery_model_prepare(bytes.data(),bytes.size(),0,&retained);assert(model&&retained);
   serial[n]=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);parallel[n]=art3m1s_recovery_create_prepared("sample",model,960,544,nullptr,nullptr);assert(serial[n]&&parallel[n]);art3m1s_recovery_model_release(model);
   // Compare scheduling with identical explicit RNG inputs. Production uses
   // distinct instance IDs so independent players do not blink in lockstep.
   for(auto id:{serial[n],parallel[n]}){Selection s(*find(id));model_controls_seed(100+n);timeline_seed_idle(100+n);}
  }
  // A late script write must reach capture without a further time advance.
  for(float mouth : {4.f,0.f,2.f,0.f})for(int n=0;n<3;++n){
   float args[4]={mouth,0,0,0};
   for(auto id:{serial[n],parallel[n]})assert(art3m1s_recovery_command(id,3,"face_talk",args,0));
   {Selection select(*find(serial[n]));model_controls_step(0);}
   {Selection select(*find(parallel[n]));if(model_controls_has("face_talk"))assert(model_controls_value_in(model_controls_context_current(),"face_talk")==timeline_value("face_talk"));}
   auto a=art3m1s_recovery_capture(serial[n]),b=art3m1s_recovery_capture(parallel[n]);same(a,b);
   art3m1s_recovery_packet_release(a);art3m1s_recovery_packet_release(b);
  }
  assert(!art3m1s_recovery_set_grid_steps(8)); // Active models reject startup reconfiguration.
  uint64_t bad[2]={parallel[0],parallel[0]},out[3]={1,1,1};art3m1s_recovery_capture_batch(bad,out,2);assert(!out[0]&&!out[1]);
  assert(!art3m1s_recovery_capture_begin(bad,out,2)&&!out[0]&&!out[1]);
  uint64_t duplicate[3]={parallel[0],parallel[1],parallel[0]};
  assert(!art3m1s_recovery_capture_begin(duplicate,out,3)&&!out[0]&&!out[1]&&!out[2]);
  uint64_t unknown=UINT64_MAX;art3m1s_recovery_capture_batch(&unknown,out,1);assert(!out[0]);
  for(int frame=0;frame<120;++frame){
   if(round==0&&(frame==20||frame==40||frame==60||frame==80||frame==100)){
    quality=frame==20?6:frame==40?4:frame==60?2:frame==80?1:8;
    for(int n=0;n<3;++n)for(auto id:{serial[n],parallel[n]}){Selection select(*find(id));assert(model_view_set_grid_steps(quality));}
   }
   uint64_t expected[3],ids[3];for(int n=0;n<3;++n){step(serial[n],frame);step(parallel[n],frame);expected[n]=art3m1s_recovery_capture(serial[n]);ids[n]=parallel[(n+frame)%3];}
   if(frame%4==0){
    // A single visible portrait also overlaps CPU evaluation with host work.
    uint64_t single[2]={0,0x12345678};
    if(!art3m1s_recovery_capture_begin(ids,single,1))art3m1s_recovery_capture_batch(ids,single,1);
    assert(single[1]==0x12345678);art3m1s_recovery_capture_finish();out[0]=single[0];
    art3m1s_recovery_capture_batch(ids+1,out+1,2);
   }
   else if(frame%4==1){uint64_t mixed[4]={ids[0],UINT64_MAX,ids[1],ids[2]},captured[4];art3m1s_recovery_capture_batch(mixed,captured,4);assert(!captured[1]);out[0]=captured[0];out[1]=captured[2];out[2]=captured[3];}
   else if(frame%4==2){if(!art3m1s_recovery_capture_begin(ids,out,2))art3m1s_recovery_capture_batch(ids,out,2);
    // Reading an unfinished packet joins safely; explicit finish is idempotent.
    if(frame%8==2){std::vector<unsigned> host_work(1024,17);for(auto &value:host_work)value=value*3+1;assert(host_work.back()==52);art3m1s_recovery_capture_finish();}
    assert(art3m1s_recovery_packet_prepare(out[0])==1);art3m1s_recovery_capture_finish();art3m1s_recovery_capture_finish();art3m1s_recovery_capture_batch(ids+2,out+2,1);}
   else {if(!art3m1s_recovery_capture_begin(ids,out,3))art3m1s_recovery_capture_batch(ids,out,3);
    if(frame%8==3)art3m1s_recovery_capture_finish();
    assert(art3m1s_recovery_packet_prepare(out[2])==1);art3m1s_recovery_capture_finish();}
   for(int n=0;n<3;++n){same(expected[(n+frame)%3],out[n]);
    uint32_t commandCount=0;auto cmds=em_render_packet_commands(packets.at(out[n]).packet,&commandCount);
    for(unsigned k=0;k<commandCount;++k)if(cmds[k].op==EM_RENDER_MESH)assert(cmds[k].vertex_count==4||cmds[k].vertex_count==(quality+1)*(quality+1));
    assert(art3m1s_recovery_packet_prepare(out[n])==1);}
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
  uint64_t last[3];
  if(!art3m1s_recovery_capture_begin(parallel,last,3))art3m1s_recovery_capture_batch(parallel,last,3);
  if(round==4){assert(art3m1s_recovery_shutdown());assert(!pending_capture&&upload_count==release_count&&instances.empty()&&packets.empty());continue;}
  // Retirement from another thread cannot destroy views used by pending jobs.
  std::thread retire([&]{art3m1s_recovery_destroy(parallel[0]);});retire.join();
  for(int n=0;n<3;++n){art3m1s_recovery_destroy(serial[n]);art3m1s_recovery_destroy(parallel[n]);}assert(art3m1s_recovery_collect());
  for(auto packet:last){assert(art3m1s_recovery_packet_prepare(packet)==1);art3m1s_recovery_packet_release(packet);}
  assert(art3m1s_recovery_shutdown());assert(upload_count==release_count&&instances.empty()&&packets.empty());
 }
 assert(art3m1s_recovery_set_grid_steps(8));
 printf("PASS grid1/2/4/6/8 and cache transitions; parallel packet comparisons=%u reload/shutdown=5 owner-only resources=%llu/%llu\n",checks,(unsigned long long)upload_count,(unsigned long long)release_count);
}
