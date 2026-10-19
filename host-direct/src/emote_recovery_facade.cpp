#include "emote_recovery_facade.h"
#include "recovered_eval_pool.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <limits>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
using ThreadId=int;
static ThreadId thread_id(){return sceKernelGetThreadId();}
#else
#include <thread>
using ThreadId=std::thread::id;
static ThreadId thread_id(){return std::this_thread::get_id();}
#endif
#pragma push_macro("_Static_assert")
#undef _Static_assert
#define _Static_assert static_assert
extern "C" {
#include "model_view.h"
#include "timeline.h"
#include "model_controls.h"
#include "texture_host.h"
#include "render_packet.h"
const EmTextureHost *art3m1s_gxm_recovered_texture_host(void);
int art3m1s_gxm_prepare_recovered_packet(const EmRenderPacket *);
int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket *,const float *,float,const float *);
}
#pragma pop_macro("_Static_assert")
#include "emote_script_timeline.hpp"
namespace direct {void log(const char *,...);}
namespace {
std::atomic_flag mutex=ATOMIC_FLAG_INIT;
struct Lock {Lock(){while(mutex.test_and_set(std::memory_order_acquire)) {}}~Lock(){mutex.clear(std::memory_order_release);}};
ThreadId owner{};bool owner_set=false;uint64_t next_id=0;
bool on_owner(){return owner_set&&owner==thread_id();}
uint64_t profile[6]{},finish_commands[2]{};
uint64_t profile_now(){
#ifdef __vita__
 return sceKernelGetSystemTimeWide();
#else
 return 0;
#endif
}
struct ProfileScope {unsigned slot;uint64_t started;
 explicit ProfileScope(unsigned s):slot(s),started(profile_now()){}
 ~ProfileScope(){profile[slot]+=profile_now()-started;if(slot<2)++profile[4+slot];}
};
// Preparation is CPU-only and may run on the source-prefetch worker. This
// registry never holds its lock while compiling, releasing or using GXM.
std::atomic_flag model_mutex=ATOMIC_FLAG_INIT;
struct ModelLock {ModelLock(){while(model_mutex.test_and_set(std::memory_order_acquire)){}}~ModelLock(){model_mutex.clear(std::memory_order_release);}};
struct CpuModel {
 std::vector<uint8_t> bytes;EmPsb psb{};EmPsbValue root{};EmScene scene{};
 ~CpuModel(){em_scene_free(&scene);}
};
auto &cpu_models=*new std::map<uint64_t,std::shared_ptr<CpuModel>>;
uint64_t next_model=0;
// A pending texture can outlive both the view and its cache entry. Keep the
// immutable model AND the Rust source/budget lease until the last borrower
// is done. This object is touched only under the render-owner lock.
struct SourceLifetime {
 std::shared_ptr<CpuModel> model;unsigned refs=1;bool adopted=false;
 void *lease;void (*release)(void*);
 SourceLifetime(std::shared_ptr<CpuModel> m,void *p,void (*r)(void*)):model(std::move(m)),lease(p),release(r){}
 ~SourceLifetime(){model.reset();if(adopted&&release)release(lease);}
 static void retain(void *p){auto *s=static_cast<SourceLifetime*>(p);if(s->refs==UINT32_MAX)std::abort();++s->refs;}
 static void drop(void *p){auto *s=static_cast<SourceLifetime*>(p);if(!--s->refs)delete s;}
};
struct Instance {
 std::shared_ptr<CpuModel> prepared;
 SourceLifetime *source_lifetime=nullptr;
 std::vector<uint8_t> bytes;EmPsb psb{};EmPsbValue root{};
 EmTimelineContext *timeline=nullptr;EmControlsContext *controls=nullptr;EmViewContext *view=nullptr;
 float center_x=480,center_y=272;
 float scale=1,x=0,y=0;bool retired=false;
 ~Instance(){if(view)model_view_context_destroy(view);if(controls)model_controls_context_destroy(controls);if(timeline)timeline_context_destroy(timeline);prepared.reset();if(source_lifetime)SourceLifetime::drop(source_lifetime);}
};
struct Packet {EmRenderPacket *packet=nullptr;bool retired=false;};
auto &instances=*new std::map<uint64_t,std::unique_ptr<Instance>>;
auto &packets=*new std::map<uint64_t,Packet>;
struct Selection {
 EmTimelineContext *time;EmControlsContext *controls;EmViewContext *view;
 Selection(Instance &i):time(timeline_context_use(i.timeline)),controls(model_controls_context_use(i.controls)),view(model_view_context_use(i.view)){}
 ~Selection(){model_view_context_use(view);model_controls_context_use(controls);timeline_context_use(time);}
};
RecoveredEvalPool evaluation_pool;
struct Evaluation {Instance *instance;int valid=0;};
void evaluate_instance(void *arg){auto &job=*static_cast<Evaluation*>(arg);Selection select(*job.instance);job.valid=model_view_evaluate(960,544,-1,-1);}
// Each evaluation publishes immutable sprite/deformation inputs. All three
// CPUs may then claim disjoint vertex ranges, including while another model
// is still evaluating. Packets, texture leases and the registry stay on owner.
struct PipelineModel {
 Evaluation evaluation{};unsigned cuts[EM_VIEW_VERTEX_PARTS+1]{};
 std::atomic<unsigned> ready{0},next{0};uint64_t started=0,ended[EM_VIEW_VERTEX_PARTS]{};
 EmViewVertexStats stats[EM_VIEW_VERTEX_PARTS]{};
};
struct EvaluationPipeline {PipelineModel models[3];unsigned count=0;std::atomic<unsigned> next_model{0};};
void evaluate_next_model(EvaluationPipeline &batch){
 unsigned index=batch.next_model.fetch_add(1,std::memory_order_relaxed);
 if(index<batch.count){auto &m=batch.models[index];evaluate_instance(&m.evaluation);
  Selection select(*m.evaluation.instance);unsigned parts=m.evaluation.valid?model_view_partition_vertices(m.cuts):0;
  m.started=profile_now();m.ready.store(parts+1,std::memory_order_release);
 }
}
void run_pipeline(void *arg){auto &batch=*static_cast<EvaluationPipeline*>(arg);
 evaluate_next_model(batch);
 for(;;){bool pending=false,worked=false;
  for(unsigned n=0;n<batch.count;++n){auto &m=batch.models[n];unsigned ready=m.ready.load(std::memory_order_acquire);
   if(!ready){pending=true;continue;}unsigned part=m.next.load(std::memory_order_relaxed);
   while(part<ready-1){if(m.next.compare_exchange_weak(part,part+1,std::memory_order_relaxed)){
    Selection select(*m.evaluation.instance);model_view_run_vertices(m.cuts[part],m.cuts[part+1],960,544,m.stats+part);
    m.ended[part]=profile_now();worked=true;break;
   }}
  }
  // Claimed ranges may still be running; the owner joins both workers before
  // reading any result or retiring inputs. No completion counter is required.
  if(!pending&&!worked)return;
 }
}
struct EvaluationAffinity {
#ifdef __vita__
 int owner=sceKernelGetThreadId(),previous=sceKernelGetThreadCpuAffinityMask(owner);bool changed=false;
 EvaluationAffinity(){if(previous>0&&(previous&SCE_KERNEL_CPU_MASK_USER_0)&&previous!=SCE_KERNEL_CPU_MASK_USER_0)changed=sceKernelChangeThreadCpuAffinityMask(owner,SCE_KERNEL_CPU_MASK_USER_0)>=0;}
 ~EvaluationAffinity(){if(changed&&sceKernelChangeThreadCpuAffinityMask(owner,previous)<0)std::abort();}
#endif
};
uint64_t token();
struct PendingCapture {EvaluationPipeline batch;uint64_t handles[3]{},started=0;EvaluationAffinity affinity;};
std::unique_ptr<PendingCapture> pending_capture;
void finish_pending(){if(!pending_capture)return;
 auto &p=*pending_capture;run_pipeline(&p.batch);evaluation_pool.join(0);evaluation_pool.join(1);
 for(unsigned n=0;n<p.batch.count;++n){auto &m=p.batch.models[n];auto &packet=packets.at(p.handles[n]);if(!m.evaluation.valid)continue;
  EmViewVertexStats total{};uint64_t end=m.started;unsigned parts=m.ready.load(std::memory_order_relaxed)-1;
  for(unsigned k=0;k<parts;++k){auto &s=m.stats[k];total.reused+=s.reused;total.rebuilt+=s.rebuilt;
   for(unsigned j=0;j<6;++j)total.changes[j]+=s.changes[j];total.geometry_us+=s.geometry_us;total.vertices_us+=s.vertices_us;if(m.ended[k]>end)end=m.ended[k];}
  total.wall_us=end-m.started;Selection select(*m.evaluation.instance);model_view_accept_vertices(960,544,&total);packet.packet=model_view_capture_evaluated();
 }
 // Includes overlapped host work; do not add this timer to host scene time.
 profile[0]+=profile_now()-p.started;profile[4]+=p.batch.count;pending_capture.reset();
}
uint64_t register_packet(EmRenderPacket *p){if(!p)return 0;
 try{uint64_t t=token();if(!t){em_render_packet_destroy(p);return 0;}packets.emplace(t,Packet{p,false});return t;}catch(...){em_render_packet_destroy(p);return 0;}}
void collect(){finish_pending();for(auto i=packets.begin();i!=packets.end();)if(i->second.retired){em_render_packet_destroy(i->second.packet);i=packets.erase(i);}else ++i;
 for(auto i=instances.begin();i!=instances.end();)if(i->second->retired)i=instances.erase(i);else ++i;}
Instance *find(uint64_t id){auto i=instances.find(id);return i==instances.end()||i->second->retired?nullptr:i->second.get();}
uint64_t token(){return next_id==std::numeric_limits<uint64_t>::max()?0:++next_id;}
bool header(const uint8_t *p,size_t n){if(!p||n<40||n>UINT32_MAX||memcmp(p,"PSB\0",4))return false;
 uint16_t version;memcpy(&version,p+4,2);if(version<2||version>4||(version==4&&n<56))return false;
 for(unsigned o=8;o<40;o+=4){uint32_t v;memcpy(&v,p+o,4);if(v>=n)return false;}return true;}
}
static uint64_t model_prepare(const uint8_t *data,size_t size,size_t limit,size_t *retained,bool shared){
 if(retained)*retained=0;if(!retained||!header(data,size))return 0;
 const size_t overhead=sizeof(CpuModel)+128;
 if(limit&&(!shared?size:0)+overhead>=limit)return 0;
 try {auto model=std::make_shared<CpuModel>();if(!shared)model->bytes.assign(data,data+size);
  model->psb.raw=shared?const_cast<uint8_t*>(data):model->bytes.data();model->psb.length=uint32_t(size);em_psb_init_header(&model->psb);em_psb_root(&model->root,&model->psb);
  if(!model_view_compile_scene(&model->scene,model->root,limit?limit-model->bytes.capacity()-overhead:0))return 0;
  const size_t cost=sizeof(CpuModel)+model->bytes.capacity()+model->scene.bytes+128;
  ModelLock lock;if(next_model==UINT64_MAX)return 0;uint64_t id=++next_model;cpu_models.emplace(id,std::move(model));*retained=cost;return id;
 }catch(...){return 0;}
}
extern "C" uint64_t art3m1s_recovery_model_prepare(const uint8_t *data,size_t size,size_t limit,size_t *retained){return model_prepare(data,size,limit,retained,false);}
extern "C" uint64_t art3m1s_recovery_model_prepare_shared(const uint8_t *data,size_t size,size_t limit,size_t *retained){return model_prepare(data,size,limit,retained,true);}
extern "C" void art3m1s_recovery_model_release(uint64_t id){
 std::shared_ptr<CpuModel> released;{ModelLock lock;auto i=cpu_models.find(id);if(i!=cpu_models.end()){released=std::move(i->second);cpu_models.erase(i);}}
}
extern "C" uint64_t art3m1s_recovery_create_prepared(const char *path,uint64_t model_id,uint32_t width,uint32_t height,void *lease,void (*release)(void*)){
 std::shared_ptr<CpuModel> model;{ModelLock lock;auto m=cpu_models.find(model_id);if(m!=cpu_models.end())model=m->second;}
 // Scripts create 0x0 layers to warm the model cache, then delete them.
 // Keep the zero canvas; a partially empty canvas is still invalid.
 if(!model||(!width!=!height)||width>16384||height>16384)return 0;
 Lock lock;if(owner_set&&!on_owner())return 0;if(!owner_set){owner=thread_id();owner_set=true;}
 uint64_t started=profile_now();collect();uint64_t collected=profile_now();
 try {auto i=std::unique_ptr<Instance>(new Instance);i->prepared=model;i->root=model->root;i->center_x=width*.5f;i->center_y=height*.5f;
  i->source_lifetime=new SourceLifetime(model,lease,release);
  i->timeline=timeline_context_create();i->controls=model_controls_context_create();i->view=model_view_context_create();if(!i->timeline||!i->controls||!i->view)return 0;
  uint64_t allocated=profile_now(),bound=allocated,loaded=allocated;
  {Selection select(*i);timeline_bind_script(i->root);bound=profile_now();
   const EmTextureHost *host=art3m1s_gxm_recovered_texture_host();if(!host||!model_view_texture_host(host))return 0;
   // Owned models are intrinsically safe; shared models require the caller's
   // explicit backing lease. Otherwise preserve the copying fallback.
   if(!model->bytes.empty()||release){EmTextureSourceLease backing{i->source_lifetime,SourceLifetime::retain,SourceLifetime::drop};if(!model_view_texture_source_lease(&backing))return 0;}
   model_view_material_profile(0);if(!model_view_load_precompiled(i->root,&model->scene))return 0;loaded=profile_now();
   if(!model_view_projection(1,i->center_x,i->center_y))return 0;}
  uint64_t id=token();if(!id)return 0;{Selection select(*i);model_controls_seed(id);}auto *instance=i.get();instances.emplace(id,std::move(i));
  // Failed creation leaves the incoming lease with its caller. Successful
  // creation transfers it, including to any deferred texture borrowers.
  instance->source_lifetime->adopted=true;
  direct::log("[recovered-load] path=%s bytes=%u collect_us=%llu copy_us=0 contexts_us=%llu bind_us=%llu view_us=%llu total_us=%llu prepared=1",path?path:"",unsigned(model->psb.length),(unsigned long long)(collected-started),(unsigned long long)(allocated-collected),(unsigned long long)(bound-allocated),(unsigned long long)(loaded-bound),(unsigned long long)(profile_now()-started));return id;
 }catch(...){return 0;}
}
extern "C" uint64_t art3m1s_recovery_create(const char *path,const uint8_t *data,size_t size,uint32_t width,uint32_t height){
 Lock lock;if(owner_set&&!on_owner())return 0;if((!width!=!height)||width>16384||height>16384||!header(data,size))return 0;
 uint64_t started=profile_now();
 if(!owner_set){owner=thread_id();owner_set=true;}collect();uint64_t collected=profile_now();
 try {auto i=std::unique_ptr<Instance>(new Instance);i->center_x=width*.5f;i->center_y=height*.5f;i->bytes.assign(data,data+size);i->psb.raw=i->bytes.data();i->psb.length=(uint32_t)size;em_psb_init_header(&i->psb);em_psb_root(&i->root,&i->psb);uint64_t copied=profile_now();
  i->timeline=timeline_context_create();i->controls=model_controls_context_create();i->view=model_view_context_create();if(!i->timeline||!i->controls||!i->view)return 0;uint64_t allocated=profile_now(),bound=allocated,loaded=allocated;
  {Selection select(*i);timeline_bind_neutral(i->root);model_controls_bind(i->root);bound=profile_now();
   const EmTextureHost *host=art3m1s_gxm_recovered_texture_host();if(!host||!model_view_texture_host(host))return 0;
   // No filename-based provenance inference: unknown profile stays zero.
   model_view_material_profile(0);if(!model_view_load(i->root))return 0;loaded=profile_now();
   if(!model_view_projection(1,i->center_x,i->center_y))return 0;}
  uint64_t id=token();if(!id)return 0;{Selection select(*i);model_controls_seed(id);}instances.emplace(id,std::move(i));
  direct::log("[recovered-load] path=%s bytes=%u collect_us=%llu copy_us=%llu contexts_us=%llu bind_us=%llu view_us=%llu total_us=%llu",path?path:"",unsigned(size),(unsigned long long)(collected-started),(unsigned long long)(copied-collected),(unsigned long long)(allocated-copied),(unsigned long long)(bound-allocated),(unsigned long long)(loaded-bound),(unsigned long long)(profile_now()-started));return id;
 }catch(...){return 0;}
}
extern "C" void art3m1s_recovery_destroy(uint64_t id){Lock lock;auto i=instances.find(id);if(i!=instances.end())i->second->retired=true;}
extern "C" int art3m1s_recovery_collect(){Lock lock;if(!on_owner())return 0;collect();return 1;}
extern "C" void art3m1s_recovery_take_profile(uint64_t values[6]){Lock lock;if(!values)return;std::memcpy(values,profile,sizeof profile);std::memset(profile,0,sizeof profile);
 if(finish_commands[0]||finish_commands[1]){direct::log("[recovered-finish] step=%llu skip=%llu at_us=%llu",(unsigned long long)finish_commands[0],(unsigned long long)finish_commands[1],(unsigned long long)profile_now());finish_commands[0]=finish_commands[1]=0;}}
extern "C" int art3m1s_recovery_prepare_resources(){Lock lock;if(!owner_set)return 1;if(!on_owner())return -1;ProfileScope timing(2);collect();uint64_t collected=profile_now();int result=1;
 for(auto &entry:instances){Selection select(*entry.second);int ready=model_view_prepare_host_textures();if(ready<0)return -1;if(!ready)result=0;}
 // A transition can retain a packet after its model/view was destroyed.
 for(auto &entry:packets){int ready=art3m1s_gxm_prepare_recovered_packet(entry.second.packet);if(ready<0)return -1;if(!ready)result=0;}
 if(profile_now()-timing.started>20000)direct::log("[recovered-resources] instances=%u packets=%u total_us=%llu collect_us=%llu upload_us=%llu ready=%d",unsigned(instances.size()),unsigned(packets.size()),(unsigned long long)(profile_now()-timing.started),(unsigned long long)(collected-timing.started),(unsigned long long)(profile_now()-collected),result);
 return result;}
extern "C" int art3m1s_recovery_command(uint64_t id,uint32_t op,const char *label,const float args[4],uint32_t flags){
 Lock lock;if(!on_owner()||!args)return 0;finish_pending();Instance *i=find(id);if(!i)return 0;for(unsigned k=0;k<4;++k)if(!std::isfinite(args[k]))return 0;
 Selection select(*i);switch(op){
 case 1:if(args[1]!=0||args[2]!=0)return 0;if(!model_view_projection(args[0],i->center_x+i->x,i->center_y+i->y))return 0;i->scale=args[0];return 1;
 case 2:if(args[2]!=0||args[3]!=0)return 0;if(!model_view_projection(i->scale,i->center_x+args[0],i->center_y+args[1]))return 0;i->x=args[0];i->y=args[1];return 1;
 case 3:return label&&timeline_set_variable(label,args[0],args[1],args[2],0);
 case 4:case 5:case 6:case 7:{
  int result=emote_script_timeline(op,label,args,flags);
  if(result==2)direct::log("[recovered-timeline] absent optional action op=%u label=%s flags=%u available=%d",op,label?label:"",flags,timeline_count());
  return result!=0;}
 case 8:timeline_pass();return 1;
 case 10:case 11:if(!timeline_finish_playback(op==11))return 0;model_controls_reset_physics();timeline_finish_variables();model_controls_step(0);++finish_commands[op-10];return 1;
 case 9:return args[0]==1; // Current recovered tessellation is fixed.
 default:return 0;}}
extern "C" int art3m1s_recovery_query(uint64_t id,uint32_t op,const char *label,float *out){Lock lock;if(!on_owner()||!out)return 0;finish_pending();Instance *i=find(id);if(!i)return 0;Selection select(*i);
 switch(op){case 1:if(!label)return 0;*out=timeline_value(label);return 1;case 2:if(!label)return 0;*out=timeline_is_playing(label)?1.f:0.f;return 1;case 3:*out=1;return 1;default:return 0;}}
extern "C" int art3m1s_recovery_advance(uint64_t id,float frames){Lock lock;if(!on_owner()||!std::isfinite(frames)||frames<0)return 0;ProfileScope timing(3);collect();Instance *i=find(id);if(!i)return 0;
#ifdef DIRECT_RECOVERED_FREEZE_PROBE
 // Separate diagnostic build only. Keep all geometry/materials and capture
 // evaluation intact while holding timeline, blink and physics clocks fixed.
 return 1;
#else
 Selection select(*i);timeline_step(frames);model_view_motion_step(frames);model_controls_step(frames);return 1;
#endif
}
extern "C" uint64_t art3m1s_recovery_capture(uint64_t id){Lock lock;if(!on_owner())return 0;ProfileScope timing(0);collect();Instance *i=find(id);if(!i)return 0;Selection select(*i);return register_packet(model_view_capture(-1,-1));}
extern "C" void art3m1s_recovery_capture_batch(const uint64_t *ids,uint64_t *out,unsigned count){
 if(!out)return;for(unsigned n=0;n<count;++n)out[n]=0;
 Lock lock;if(!on_owner()||!ids||count>64)return;collect();if(!count)return;
 // Duplicate contexts cannot be evaluated concurrently. Reject the entire batch.
 for(unsigned n=0;n<count;++n)for(unsigned k=0;k<n;++k)if(ids[n]==ids[k])return;
 uint64_t started=profile_now();bool starting=count>1&&!evaluation_pool.attempted;bool parallel=count>1&&evaluation_pool.init();
 if(starting)direct::log("[recovered-evaluation-workers] ready=%d count=2 joined_batches=1 owner_only_packets=1 geometry_pipeline=1",parallel?1:0);
 for(unsigned base=0;base<count;base+=3){Evaluation jobs[3]{};unsigned used=count-base<3?count-base:3;
  for(unsigned n=0;n<used;++n)jobs[n].instance=find(ids[base+n]);
  if(parallel){EvaluationPipeline batch;unsigned positions[3]{};
   for(unsigned n=0;n<used;++n)if(jobs[n].instance){positions[batch.count]=n;batch.models[batch.count++].evaluation.instance=jobs[n].instance;}
   if(batch.count){EvaluationAffinity affinity;
    evaluation_pool.dispatch(0,run_pipeline,&batch);evaluation_pool.dispatch(1,run_pipeline,&batch);run_pipeline(&batch);
    evaluation_pool.join(0);evaluation_pool.join(1);
   }
   for(unsigned n=0;n<batch.count;++n){auto &m=batch.models[n];jobs[positions[n]].valid=m.evaluation.valid;if(!m.evaluation.valid)continue;
    EmViewVertexStats total{};uint64_t end=m.started;unsigned parts=m.ready.load(std::memory_order_relaxed)-1;
    for(unsigned k=0;k<parts;++k){auto &s=m.stats[k];total.reused+=s.reused;total.rebuilt+=s.rebuilt;
     for(unsigned j=0;j<6;++j)total.changes[j]+=s.changes[j];total.geometry_us+=s.geometry_us;total.vertices_us+=s.vertices_us;if(m.ended[k]>end)end=m.ended[k];}
    total.wall_us=end-m.started;Selection select(*m.evaluation.instance);model_view_accept_vertices(960,544,&total);
   }
  }else for(unsigned n=0;n<used;++n)if(jobs[n].instance)evaluate_instance(jobs+n);
  // All CPU jobs have finished before packet lease changes. Single/failed-pool
  // batches retain the original per-model vertex pool fallback.
  for(unsigned n=0;n<used;++n)if(jobs[n].valid){Selection select(*jobs[n].instance);out[base+n]=register_packet(model_view_capture_evaluated());}
 }
 profile[0]+=profile_now()-started;profile[4]+=count;
}
extern "C" int art3m1s_recovery_capture_begin(const uint64_t *ids,uint64_t *out,unsigned count){
 if(!out)return 0;for(unsigned n=0;n<count;++n)out[n]=0;
 Lock lock;if(!on_owner()||!ids||count<1||count>3)return 0;collect();
 Instance *models[3]{};
 for(unsigned n=0;n<count;++n){for(unsigned k=0;k<n;++k)if(ids[n]==ids[k])return 0;
  models[n]=find(ids[n]);if(!models[n])return 0;}
 bool starting=!evaluation_pool.attempted;bool ready=evaluation_pool.init();
 if(starting)direct::log("[recovered-evaluation-workers] ready=%d count=2 joined_batches=1 owner_only_packets=1 geometry_pipeline=1 host_overlap=1",ready?1:0);
 if(!ready)return 0;
 auto p=std::unique_ptr<PendingCapture>(new(std::nothrow) PendingCapture);if(!p)return 0;
 p->batch.count=count;for(unsigned n=0;n<count;++n)p->batch.models[n].evaluation.instance=models[n];
 try{for(unsigned n=0;n<count;++n){uint64_t id=token();if(!id)throw std::bad_alloc();packets.emplace(id,Packet{});p->handles[n]=id;}}
 catch(...){for(auto id:p->handles)if(id)packets.erase(id);return 0;}
 p->started=profile_now();pending_capture=std::move(p);
 evaluation_pool.dispatch(0,run_pipeline,&pending_capture->batch);evaluation_pool.dispatch(1,run_pipeline,&pending_capture->batch);
 // Start all three evaluations immediately. Once the owner's model publishes
 // its inputs, workers can process its vertices during host scene construction.
 // Finish still joins every worker before packet creation or resource changes.
 if(count==3)evaluate_next_model(pending_capture->batch);
 for(unsigned n=0;n<count;++n)out[n]=pending_capture->handles[n];return 1;
}
extern "C" void art3m1s_recovery_capture_finish(){Lock lock;if(on_owner())finish_pending();}
extern "C" void art3m1s_recovery_packet_release(uint64_t id){Lock lock;auto p=packets.find(id);if(p!=packets.end())p->second.retired=true;}
extern "C" int art3m1s_recovery_packet_prepare(uint64_t id){Lock lock;if(!on_owner())return -1;collect();auto p=packets.find(id);if(p==packets.end()||p->second.retired||!p->second.packet)return -1;return art3m1s_gxm_prepare_recovered_packet(p->second.packet);}
// Neutral parent elimination is valid only for source-over packet materials.
// Additive/subtractive/destination-dependent meshes must retain isolation.
extern "C" int art3m1s_recovery_packet_source_over(uint64_t id){Lock lock;if(!on_owner())return 0;finish_pending();auto p=packets.find(id);if(p==packets.end()||p->second.retired||!p->second.packet)return 0;
 uint32_t count=0;const EmRenderCommand *commands=em_render_packet_commands(p->second.packet,&count);
 for(uint32_t n=0;n<count;++n)if(commands[n].op==EM_RENDER_MATERIAL){uint32_t m=uint32_t(commands[n].args[0]);if(m!=UINT32_MAX&&m!=0&&m!=16)return 0;}
 return 1;}
extern "C" int art3m1s_recovery_packet_draw(uint64_t id,const float affine[6],float opacity,const float *clip){Lock lock;if(!on_owner())return 0;finish_pending();ProfileScope timing(1);auto p=packets.find(id);if(p==packets.end()||p->second.retired||!p->second.packet)return 0;return art3m1s_gxm_draw_recovered_packet(p->second.packet,affine,opacity,clip);}

extern "C" int art3m1s_recovery_shutdown(){Lock lock;if(!on_owner())return 0;finish_pending();evaluation_pool.shutdown();for(auto &p:packets)p.second.retired=true;for(auto &i:instances)i.second->retired=true;collect();owner_set=false;return 1;}

extern "C" void art3m1s_recovery_take_pose_profile(uint64_t out[6]){Lock lock;if(!out)return;std::memset(out,0,6*sizeof(*out));if(on_owner())model_view_take_pose_profile(out);}

extern "C" void art3m1s_recovery_take_dirty_profile(uint64_t out[7]){Lock lock;if(!out)return;std::memset(out,0,7*sizeof(*out));if(on_owner())model_view_take_dirty_profile(out);}
extern "C" void art3m1s_recovery_take_pose_parts(uint64_t out[10]){Lock lock;if(!out)return;std::memset(out,0,10*sizeof(*out));if(on_owner())for(auto &entry:instances){Selection select(*entry.second);uint64_t part[10];model_view_take_pose_parts(part);for(unsigned n=0;n<10;++n)out[n]+=part[n];}}
