#include "emote_recovery_facade.h"
#include "emote_api_log.hpp"
#include "recovered_eval_pool.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <deque>
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
int art3m1s_gxm_draw_recovered_packet(const EmRenderPacket *,const float *,float,const float *,float,int);
}
#pragma pop_macro("_Static_assert")
#include "emote_script_timeline.hpp"
namespace direct {void log(const char *,...);}
namespace {
std::atomic_flag mutex=ATOMIC_FLAG_INIT;
struct Lock {Lock(){while(mutex.test_and_set(std::memory_order_acquire)) {}}~Lock(){mutex.clear(std::memory_order_release);}};
ThreadId owner{};bool owner_set=false;uint64_t next_id=0;
bool on_owner(){return owner_set&&owner==thread_id();}
uint64_t profile[6]{},finish_commands[2]{};unsigned configured_grid_steps=4;
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
constexpr float full_turn = 0x1.921fb6p+2f;
template<std::size_t Channels,bool Angular=false>
struct PlayerTransition {
 using Values=std::array<float,Channels>;
 struct Request {Values target;float frames,exponent;};
 Values value{},start{},target{},delta{};
 float duration=0,elapsed=0,exponent=1;
 bool pending=false;
 std::deque<Request> queued;

 PlayerTransition()=default;
 explicit PlayerTransition(Values initial):value(initial){}

 bool active() const {return pending||duration>0||!queued.empty();}

 void adopt(PlayerTransition &prepared) noexcept {
  value=prepared.value;start=prepared.start;target=prepared.target;delta=prepared.delta;
  duration=prepared.duration;elapsed=prepared.elapsed;exponent=prepared.exponent;
  pending=prepared.pending;queued.swap(prepared.queued);
 }

 void begin(Request request) {
  start=value;target=request.target;
  for(std::size_t i=0;i<Channels;++i)delta[i]=target[i]-value[i];
  if constexpr(Angular)delta[0]=std::remainder(delta[0],full_turn);
  duration=request.frames;elapsed=0;exponent=request.exponent;
 }
 void set(Values requested,float frames,float easing,bool append) {
  if(frames<=0){queued.clear();value=requested;duration=0;pending=false;return;}
  Request request{requested,frames,easing>=0?1+easing:1/(1-easing)};
  if(append&&(duration>0||!queued.empty())){queued.push_back(request);return;}
  if(duration>0&&requested==target&&frames>duration-elapsed)return;
  queued.clear();
  begin(request);
  pending=true;
 }
 void advance(float frames) {
  if(pending){pending=false;return;}
  if(duration<=0){
   if(!queued.empty()){begin(queued.front());queued.pop_front();}
   return;
  }
  elapsed+=frames;
  if(elapsed>=duration){value=target;duration=0;return;}
  float weight=std::pow(elapsed/duration,exponent);
  for(std::size_t i=0;i<Channels;++i){
   if(std::isfinite(delta[i]))value[i]=start[i]+delta[i]*weight;
   else value[i]=static_cast<float>(double(start[i])+(double(target[i])-start[i])*weight);
  }
 }
};
uint32_t packed_player_color(const PlayerTransition<4>::Values &channels) {
 uint32_t rgba=0;
 for(float channel:channels)rgba=(rgba<<8)|static_cast<uint32_t>(channel);
 return rgba;
}
struct PlayerGraphics { bool smoothing=true; };
struct Instance {
 std::shared_ptr<CpuModel> prepared;
 SourceLifetime *source_lifetime=nullptr;
 std::vector<uint8_t> bytes;EmPsb psb{};EmPsbValue root{};
 EmTimelineContext *timeline=nullptr;EmControlsContext *controls=nullptr;EmViewContext *view=nullptr;
 float center_x=480,center_y=272;
 float mesh_ratio=1;bool retired=false;
 bool queuing=false;
 PlayerTransition<1> scale{{1}};
 PlayerTransition<2> coord;
 PlayerTransition<1,true> rotation;
 PlayerTransition<4> color{{128,128,128,255}};
 PlayerTransition<1> grayscale,outer_rotation;
 std::array<PlayerTransition<2>,3> outer_forces;
 bool explicit_scale_mode=false,original_scale=false,blink_enabled=true,stereovision=false;
 bool detached=false;
 std::shared_ptr<PlayerGraphics> graphics=std::make_shared<PlayerGraphics>();
 float stereo_volume=1,stereo_parallax=0.2f;
 bool track_modifications=false,modified=true,have_frame_hash=false;
 uint64_t frame_hash=0;
 emote_api_log::Log api_log;
 ~Instance(){if(view)model_view_context_destroy(view);if(controls)model_controls_context_destroy(controls);if(timeline)timeline_context_destroy(timeline);prepared.reset();if(source_lifetime)SourceLifetime::drop(source_lifetime);}
};
using LogOperation=emote_api_log::Operation;
void record_api(Instance &i,LogOperation op,const std::array<float,4> &args={},uint32_t flags=0,
                const char *label=nullptr,const char *key=nullptr){
 if(!i.api_log.recording())return;
 try{i.api_log.record({op,label?label:"",key?key:"",args,flags});}
 catch(const std::bad_alloc&){i.api_log.stop();}
}
struct Packet {EmRenderPacket *packet=nullptr;bool retired=false;float grayscale=0;bool smoothing=true;};
auto &instances=*new std::map<uint64_t,std::unique_ptr<Instance>>;
auto &packets=*new std::map<uint64_t,Packet>;
struct Selection {
 EmTimelineContext *time;EmControlsContext *controls;EmViewContext *view;
 Selection(Instance &i):time(timeline_context_use(i.timeline)),controls(model_controls_context_use(i.controls)),view(model_view_context_use(i.view)){}
 ~Selection(){model_view_context_use(view);model_controls_context_use(controls);timeline_context_use(time);}
};
float authored_scale(){
 const float scale=model_controls_authored_scale();
 return std::isfinite(scale)&&scale!=0?scale:1;
}
float effective_scale(const Instance &i,float scale){
 return i.explicit_scale_mode&&!i.original_scale?scale*authored_scale():scale;
}
bool project(const Instance &i){
 return model_view_projection(effective_scale(i,i.scale.value[0]),
  i.center_x+i.coord.value[0],i.center_y+i.coord.value[1])!=0;
}
float sampled_grayscale(const Instance &i){
 const float clamped=std::clamp(i.grayscale.value[0],0.f,1.f);
 return static_cast<unsigned>(clamped*255.f)/255.f;
}
constexpr unsigned vita_stereo_screen=0;
void apply_stereo(const Instance &i){
 model_controls_set_stereovision(i.stereovision,i.stereo_volume*i.stereo_parallax,vita_stereo_screen,2);
}
void apply_outer_force(const Instance &i,unsigned group){
 const auto &force=i.outer_forces[group].value;
 model_controls_set_outer_force(group,force[0],force[1]);
}
int outer_force_group(const char *label){
 if(!std::strcmp(label,"bust"))return 0;
 if(!std::strcmp(label,"hair"))return 1;
 if(!std::strcmp(label,"parts"))return 2;
 return -1;
}
bool is_animating(const Instance &i){
 return timeline_is_animating()||i.scale.active()||i.coord.active()||i.rotation.active()||i.color.active()||i.grayscale.active()||
  i.outer_rotation.active()||std::any_of(i.outer_forces.begin(),i.outer_forces.end(),[](const auto &force){return force.active();});
}
Packet capture_settings(Instance &i,EmRenderPacket *packet){
 Packet result{packet,false,sampled_grayscale(i),i.graphics->smoothing};
 if(!packet||!i.track_modifications)return result;
 // Compare the immutable draw inputs after evaluation, independently of capture mode.
 uint64_t hash=14695981039346656037ull;
 const auto add=[&hash](const void *data,size_t size){
  const auto *bytes=static_cast<const unsigned char*>(data);
  for(size_t n=0;n<size;++n){hash^=bytes[n];hash*=1099511628211ull;}
 };
 uint32_t count=0;
 const auto *commands=em_render_packet_commands(packet,&count);
 add(&count,sizeof count);add(commands,count*sizeof(*commands));
 const auto *vertices=em_render_packet_vertices(packet,&count);
 add(&count,sizeof count);add(vertices,count*sizeof(*vertices));
 const auto *indices=em_render_packet_indices(packet,&count);
 add(&count,sizeof count);add(indices,count*sizeof(*indices));
 for(uint32_t n=0;n<em_render_packet_texture_count(packet);++n){
  EmRenderTextureInfo info{};
  if(em_render_packet_texture(packet,n,&info))add(&info,sizeof info);
 }
 add(&result.grayscale,sizeof result.grayscale);add(&result.smoothing,sizeof result.smoothing);
 i.modified=!i.have_frame_hash||i.frame_hash!=hash;
 i.have_frame_hash=true;
 i.frame_hash=hash;
 return result;
}
RecoveredEvalPool evaluation_pool;
struct Evaluation {Instance *instance;int valid=0;};
void evaluate_instance(void *arg){auto &job=*static_cast<Evaluation*>(arg);Selection select(*job.instance);job.valid=model_view_evaluate(960,544,-1,-1);}
// Each evaluation publishes immutable sprite/deformation inputs. All three
// CPUs may then claim disjoint vertex ranges, including while another model
// is still evaluating. Packets, texture leases and the registry stay on owner.
struct PipelineModel {
 Evaluation evaluation{};unsigned cuts[EM_VIEW_VERTEX_PARTS+1]{};
 std::atomic<unsigned> ready{0},next{0},completed{0};uint64_t started=0,ended[EM_VIEW_VERTEX_PARTS]{};
 EmViewVertexStats stats[EM_VIEW_VERTEX_PARTS]{};
};
constexpr unsigned capture_capacity=4;
struct EvaluationPipeline {PipelineModel models[capture_capacity];unsigned count=0;std::atomic<unsigned> next_model{0};};
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
  // A batch may contain more models than CPUs. Claim the remaining model
  // rather than waiting forever for an input no worker has published.
  if(batch.next_model.load(std::memory_order_relaxed)<batch.count){evaluate_next_model(batch);worked=true;}
  for(unsigned n=0;n<batch.count;++n){auto &m=batch.models[n];unsigned ready=m.ready.load(std::memory_order_acquire);
   if(!ready){pending=true;continue;}unsigned part=m.next.load(std::memory_order_relaxed);
   while(part<ready-1){if(m.next.compare_exchange_weak(part,part+1,std::memory_order_relaxed)){
    Selection select(*m.evaluation.instance);model_view_run_vertices(m.cuts[part],m.cuts[part+1],960,544,m.stats+part);
    m.ended[part]=profile_now();m.completed.fetch_add(1,std::memory_order_acq_rel);worked=true;break;
   }}
  }
  // Claimed ranges may still be running. Completed ranges publish through the
  // per-model counter; owner joins every worker before retiring batch storage.
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
struct PendingCapture {EvaluationPipeline batch;uint64_t handles[capture_capacity]{},started=0;EvaluationAffinity affinity;};
std::unique_ptr<PendingCapture> pending_capture;
void finish_pending(){if(!pending_capture)return;
 auto &p=*pending_capture;unsigned captured=0;bool sealed[capture_capacity]{};
 // Publish each model only after all its ranges complete. Packet construction
 // stays on owner while the other models continue CPU-only evaluation.
 while(captured<p.batch.count){
  if(p.batch.next_model.load(std::memory_order_relaxed)<p.batch.count)evaluate_next_model(p.batch);
  for(unsigned n=0;n<p.batch.count;++n){auto &m=p.batch.models[n];if(sealed[n])continue;
   unsigned ready=m.ready.load(std::memory_order_acquire);if(!ready)continue;
   const unsigned parts=ready-1;
   if(m.completed.load(std::memory_order_acquire)==parts){
    if(m.evaluation.valid){
     EmViewVertexStats total{};uint64_t end=m.started;
     for(unsigned k=0;k<parts;++k){auto &v=m.stats[k];total.reused+=v.reused;total.rebuilt+=v.rebuilt;
      for(unsigned j=0;j<6;++j)total.changes[j]+=v.changes[j];total.geometry_us+=v.geometry_us;total.vertices_us+=v.vertices_us;if(m.ended[k]>end)end=m.ended[k];}
     total.wall_us=end-m.started;Selection select(*m.evaluation.instance);model_view_accept_vertices(960,544,&total);
     auto &packet=packets.at(p.handles[n]);
     auto captured_packet=capture_settings(*m.evaluation.instance,model_view_capture_evaluated());
     captured_packet.retired=packet.retired;
     packet=captured_packet;
    }
    sealed[n]=true;++captured;continue;
   }
   unsigned part=m.next.load(std::memory_order_relaxed);
   while(part<parts){if(m.next.compare_exchange_weak(part,part+1,std::memory_order_relaxed)){
    Selection select(*m.evaluation.instance);model_view_run_vertices(m.cuts[part],m.cuts[part+1],960,544,m.stats+part);
    m.ended[part]=profile_now();m.completed.fetch_add(1,std::memory_order_acq_rel);break;
   }}
  }
 }
 // The last range can publish before its worker exits the queue. Join before
 // retiring batch storage, changing instances, or exposing GPU submission.
 evaluation_pool.join(0);evaluation_pool.join(1);
 // Includes overlapped host work; do not add this timer to host scene time.
 profile[0]+=profile_now()-p.started;profile[4]+=p.batch.count;pending_capture.reset();
}
uint64_t register_packet(Instance &i,EmRenderPacket *p){if(!p)return 0;
 try{uint64_t t=token();if(!t){em_render_packet_destroy(p);return 0;}packets.emplace(t,capture_settings(i,p));return t;}catch(...){em_render_packet_destroy(p);return 0;}}
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
extern "C" int art3m1s_recovery_set_grid_steps(unsigned steps){
 Lock lock;if(steps!=1&&steps!=2&&steps!=4&&steps!=6&&steps!=8)return 0;
 if(owner_set&&!on_owner())return 0;
 if(owner_set)collect();
 // Startup-only: retained packets and active models keep their own quality.
 if(!instances.empty()||!packets.empty())return 0;
 configured_grid_steps=steps;return 1;
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
   model_view_material_profile(0);model_view_set_grid_steps(configured_grid_steps);if(!model_view_load_precompiled(i->root,&model->scene))return 0;loaded=profile_now();
   if(!model_view_projection(1,i->center_x,i->center_y))return 0;}
  uint64_t id=token();if(!id)return 0;{Selection select(*i);model_controls_seed(id);timeline_seed_idle(id);}auto *instance=i.get();instances.emplace(id,std::move(i));
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
   model_view_material_profile(0);model_view_set_grid_steps(configured_grid_steps);if(!model_view_load(i->root))return 0;loaded=profile_now();
   if(!model_view_projection(1,i->center_x,i->center_y))return 0;}
  uint64_t id=token();if(!id)return 0;{Selection select(*i);model_controls_seed(id);timeline_seed_idle(id);}instances.emplace(id,std::move(i));
  direct::log("[recovered-load] path=%s bytes=%u collect_us=%llu copy_us=%llu contexts_us=%llu bind_us=%llu view_us=%llu total_us=%llu",path?path:"",unsigned(size),(unsigned long long)(collected-started),(unsigned long long)(copied-collected),(unsigned long long)(allocated-copied),(unsigned long long)(bound-allocated),(unsigned long long)(loaded-bound),(unsigned long long)(profile_now()-started));return id;
 }catch(...){return 0;}
}
extern "C" void art3m1s_recovery_destroy(uint64_t id){Lock lock;auto i=instances.find(id);if(i!=instances.end())i->second->retired=true;}
extern "C" int art3m1s_recovery_assign_state(uint64_t id,uint64_t source_id) {
 Lock lock;
 if(!on_owner())return 0;
 finish_pending();
 Instance *target=find(id);
 const Instance *source=find(source_id);
 if(!target||!source)return 0;
 if(target==source)return 1;
 const bool same_model=target->prepared&&target->prepared==source->prepared;
 try{
  auto scale=source->scale;
  auto coord=source->coord;
  auto rotation=source->rotation;
  auto color=source->color;
  auto grayscale=source->grayscale;
  auto outer_rotation=source->outer_rotation;
  auto outer_forces=source->outer_forces;
  EmTimelineContext *timeline;
  {
   Selection select(*target);
   timeline=timeline_context_assign(target->timeline,source->timeline,same_model,
                                    model_controls_has_state_variable);
   if(!timeline)return 0;
   if(same_model&&!model_view_assign_motion(source->view)){
    timeline_context_destroy(timeline);return 0;
   }
  }
  timeline_context_destroy(target->timeline);
  target->timeline=timeline;
  target->scale.adopt(scale);target->coord.adopt(coord);
  target->queuing=source->queuing;
  target->rotation.adopt(rotation);target->color.adopt(color);
  target->grayscale.adopt(grayscale);target->outer_rotation.adopt(outer_rotation);
  for(unsigned n=0;n<outer_forces.size();++n)target->outer_forces[n].adopt(outer_forces[n]);
  target->explicit_scale_mode=source->explicit_scale_mode;target->original_scale=source->original_scale;
  target->blink_enabled=source->blink_enabled;
  target->stereovision=source->stereovision;target->stereo_volume=source->stereo_volume;
  target->stereo_parallax=source->stereo_parallax;
  model_controls_assign(target->controls,source->controls,same_model);
  Selection select(*target);
  project(*target);
  model_view_rotation(target->rotation.value[0]);
  model_view_color(packed_player_color(target->color.value));
  model_controls_step(0);
  return 1;
 }catch(const std::bad_alloc&){return 0;}
}
extern "C" int art3m1s_recovery_share_graphics(uint64_t id,uint64_t source_id,int detached){
 Lock lock;
 if(!on_owner())return 0;
 finish_pending();
 auto *target=find(id);
 const auto *source=find(source_id);
 if(!target||!source)return 0;
 target->graphics=source->graphics;
 target->detached=detached!=0;
 return 1;
}
extern "C" int art3m1s_recovery_collect(){Lock lock;if(!on_owner())return 0;collect();return 1;}
extern "C" void art3m1s_recovery_take_profile(uint64_t values[6]){Lock lock;if(!values)return;std::memcpy(values,profile,sizeof profile);std::memset(profile,0,sizeof profile);
 if(finish_commands[0]||finish_commands[1]){direct::log("[recovered-finish] step=%llu skip=%llu at_us=%llu",(unsigned long long)finish_commands[0],(unsigned long long)finish_commands[1],(unsigned long long)profile_now());finish_commands[0]=finish_commands[1]=0;}}
extern "C" int art3m1s_recovery_prepare_resources(){Lock lock;if(!owner_set)return 1;if(!on_owner())return -1;ProfileScope timing(2);collect();uint64_t collected=profile_now();int result=1;
 for(auto &entry:instances){if(entry.second->detached)continue;Selection select(*entry.second);int ready=model_view_prepare_host_textures();if(ready<0)return -1;if(!ready)result=0;}
 // A transition can retain a packet after its model/view was destroyed.
 for(auto &entry:packets){int ready=art3m1s_gxm_prepare_recovered_packet(entry.second.packet);if(ready<0)return -1;if(!ready)result=0;}
 if(profile_now()-timing.started>20000)direct::log("[recovered-resources] instances=%u packets=%u total_us=%llu collect_us=%llu upload_us=%llu ready=%d",unsigned(instances.size()),unsigned(packets.size()),(unsigned long long)(profile_now()-timing.started),(unsigned long long)(collected-timing.started),(unsigned long long)(profile_now()-collected),result);
 return result;}
static int apply_command(Instance *i,uint32_t op,const char *label,const float args[4],uint32_t flags){
 try{switch(op){
 case 1:
  if(!std::isfinite(effective_scale(*i,args[0])))return 0;
  i->scale.set({args[0]},args[1],args[2],i->queuing);return project(*i);
 case 2:
  i->coord.set({args[0],args[1]},args[2],args[3],i->queuing);return project(*i);
 case 3:{if(!label||!timeline_set_variable(label,args[0],args[1],args[2],0))return 0;
  // Script callbacks can write after Tick and before capture. Refresh sampled
  // controls without advancing blink, speech or physics clocks.
  model_controls_step(0);return 1;}
 case 4:case 5:case 6:case 7:{
  int result=emote_script_timeline(op,label,args,flags);
  if(result==2)direct::log("[recovered-timeline] absent optional action op=%u label=%s flags=%u available=%d",op,label?label:"",flags,timeline_count());
  return result!=0;}
 case 8:timeline_pass();return 1;
 case 10:case 11:if(!timeline_finish_playback(op==11))return 0;model_controls_reset_physics();timeline_finish_variables();model_controls_step(0);++finish_commands[op-10];return 1;
 case 9:{
  const float ratio=args[0];if(ratio<=0||ratio>1)return 0;
  // Scale the configured subdivision budget, never the previous reduced grid.
  // Packets support discrete topologies. Round the integer budget upward to
  // an available topology so quantization does not discard extra detail.
  const unsigned budget=unsigned(configured_grid_steps*ratio);
  unsigned steps=1;for(unsigned candidate:{1u,2u,4u,6u,8u})if(candidate>=budget){steps=candidate;break;}
  if(!model_view_set_grid_steps(steps))return 0;
  i->mesh_ratio=ratio;return 1;}
 default:return 0;}}catch(const std::bad_alloc&){return 0;}}
extern "C" int art3m1s_recovery_command(uint64_t id,uint32_t op,const char *label,const float args[4],uint32_t flags){
 Lock lock;if(!on_owner()||!args)return 0;finish_pending();Instance *i=find(id);if(!i)return 0;
 for(unsigned k=0;k<4;++k)if(!std::isfinite(args[k]))return 0;
 Selection select(*i);
 if(!apply_command(i,op,label,args,flags))return 0;
 if(i->api_log.recording()&&op>=1&&op<=11&&op!=9){
  constexpr LogOperation operations[]={LogOperation::SetScale,LogOperation::SetCoord,LogOperation::SetVariable,
   LogOperation::PlayTimeline,LogOperation::FadeInTimeline,LogOperation::FadeOutTimeline,LogOperation::StopTimeline,
   LogOperation::Pass,LogOperation::Pass,LogOperation::Step,LogOperation::Skip};
  record_api(*i,operations[op-1],{args[0],args[1],args[2],args[3]},flags,label);
 }
 return 1;
}
extern "C" int art3m1s_recovery_query(uint64_t id,uint32_t op,const char *label,float *out){Lock lock;if(!on_owner()||!out)return 0;finish_pending();Instance *i=find(id);if(!i)return 0;Selection select(*i);
 switch(op){
 case 1:if(!label)return 0;*out=timeline_script_value(label);return 1;
 case 2:if(!label)return 0;*out=timeline_is_playing(label)?1.f:0.f;return 1;
 case 3:*out=i->mesh_ratio;return 1;
 case 4:*out=float(timeline_variable_count());return 1;
 case 5:*out=i->scale.value[0];return 1;
 case 6:*out=i->rotation.value[0];return 1;
 case 7:if(!label)return 0;*out=timeline_blend_ratio(label);return 1;
 default:return 0;}}
extern "C" int art3m1s_recovery_query_player(uint64_t id,uint32_t op,uint32_t index,uint32_t frame,
                                             const char *label,const char *key,double values[2],const char **text){
 Lock lock;
 if(!on_owner()||!label||!key||!values||!text)return 0;
 finish_pending();
 Instance *instance=find(id);
 if(!instance)return 0;
 Selection select(*instance);
 switch(op){
 case 1:case 2:case 3:values[0]=timeline_label_count(op-1);break;
 case 4:case 5:case 6:*text=timeline_label_at(op-4,index);break;
 case 7:values[0]=timeline_playing_flags(index);break;
 case 8:values[0]=timeline_total_frames(label);break;
 case 9:values[0]=timeline_is_loop(label);break;
 case 10:values[0]=is_animating(*instance);break;
 case 11:values[0]=timeline_variable_frame_count(index);break;
 case 12:*text=timeline_variable_frame_label(index,frame);break;
 case 13:values[0]=timeline_variable_frame_value(index,frame);break;
 case 14:values[0]=timeline_variable_diff_value(key,label);break;
 case 15:
  if(!std::strcmp(label,"coordX"))values[0]=instance->coord.value[0];
  else if(!std::strcmp(label,"coordY"))values[0]=instance->coord.value[1];
  else if(!std::strcmp(label,"scale"))values[0]=instance->scale.value[0];
  else if(!std::strcmp(label,"rot"))values[0]=instance->rotation.value[0];
  else if(!std::strcmp(label,"colorR"))values[0]=packed_player_color(instance->color.value)>>24;
  else if(!std::strcmp(label,"colorG"))values[0]=(packed_player_color(instance->color.value)>>16)&255;
  else values[0]=0;
  break;
 case 16:values[0]=timeline_profile_available();break;
 case 17:values[0]=timeline_profile_height()/(instance->original_scale?authored_scale():1);break;
 case 18:values[0]=timeline_profile_count();break;
 case 19:{
  static thread_local char buffer[4096];
  if(!timeline_profile_label(index,buffer,sizeof buffer))return 0;
  *text=buffer;
  break;}
 case 20:values[0]=timeline_profile_value(label)/(instance->original_scale?authored_scale():1);break;
 case 21:values[0]=instance->coord.value[0];values[1]=instance->coord.value[1];break;
 case 22:values[0]=sampled_grayscale(*instance);break;
 case 23:{
  int group=outer_force_group(label);
  if(group<0){values[0]=values[1]=0;break;}
  values[0]=instance->outer_forces[group].value[0];values[1]=instance->outer_forces[group].value[1];break;}
 case 24:values[0]=instance->outer_rotation.value[0];break;
 case 25:values[0]=instance->original_scale;break;
 case 26:values[0]=instance->graphics->smoothing;break;
 case 27:instance->track_modifications=true;values[0]=instance->modified;break;
 case 28:values[0]=model_controls_suitable_clear_color();break;
 case 29:values[0]=instance->blink_enabled;break;
 case 30:values[0]=instance->stereovision;break;
 case 31:values[0]=instance->stereo_volume;break;
 case 32:values[0]=instance->stereo_parallax;break;
 case 33:values[0]=vita_stereo_screen;break;
 case 34:values[0]=instance->api_log.recording();break;
 case 35:values[0]=instance->api_log.replaying();break;
 default:return 0;
 }
 return 1;
}
static int apply_player_control(Instance *instance,uint32_t op,const char *label,const float values[4]){
 try{switch(op){
 case 1:return timeline_add_user_variable(label);
 case 2:instance->grayscale.set({values[0]},values[1],values[2],instance->queuing);return 1;
 case 3:{
  int group=outer_force_group(label);
  if(group<0)return 1;
  instance->outer_forces[group].set({values[0],values[1]},values[2],values[3],instance->queuing);
  apply_outer_force(*instance,group);return 1;}
 case 4:
  instance->outer_rotation.set({values[0]},values[1],values[2],instance->queuing);
  model_controls_set_outer_rotation(instance->outer_rotation.value[0]);return 1;
 case 5:{
  const auto original=instance->original_scale,explicit_mode=instance->explicit_scale_mode;
  instance->original_scale=values[0]!=0;instance->explicit_scale_mode=true;
  if(project(*instance))return 1;
  instance->original_scale=original;instance->explicit_scale_mode=explicit_mode;return 0;}
 case 6:instance->graphics->smoothing=values[0]!=0;return 1;
 case 7:instance->blink_enabled=values[0]!=0;model_controls_set_blink_enabled(instance->blink_enabled);model_controls_step(0);return 1;
 case 8:instance->stereovision=values[0]!=0;apply_stereo(*instance);return 1;
 case 9:instance->stereo_volume=values[0];apply_stereo(*instance);return 1;
 case 10:instance->stereo_parallax=values[0];apply_stereo(*instance);return 1;
 case 11:instance->api_log.clear();return 1;
 case 12:return instance->api_log.replace(label);
 case 13:instance->api_log.start_recording();return 1;
 case 14:instance->api_log.stop();return 1;
 case 15:instance->api_log.start_replay();return 1;
 default:return 0;
 }}catch(const std::bad_alloc&){return 0;}
}
extern "C" int art3m1s_recovery_control_player(uint64_t id,uint32_t op,const char *label,const float values[4]){
 Lock lock;
 if(!on_owner()||!label||!values)return 0;
 for(unsigned n=0;n<4;++n)if(!std::isfinite(values[n]))return 0;
 finish_pending();
 Instance *instance=find(id);
 if(!instance)return 0;
 Selection select(*instance);
 if(!apply_player_control(instance,op,label,values))return 0;
 if(op>=2&&op<=4){
  constexpr LogOperation operations[]={LogOperation::SetGrayscale,LogOperation::SetOuterForce,LogOperation::SetOuterRot};
  record_api(*instance,operations[op-2],{values[0],values[1],values[2],values[3]},0,label);
 }
 return 1;
}
extern "C" int art3m1s_recovery_physics_scale(uint64_t id,uint32_t group,float *value,int write) {
 Lock lock;
 if (!on_owner() || !value || group >= 3 || (write && !std::isfinite(*value))) return 0;
 finish_pending();
 Instance *instance = find(id);
 if (!instance) return 0;
 Selection select(*instance);
 if (write) model_controls_set_physics_scale(group, *value);
 else *value = model_controls_physics_scale(group);
 return 1;
}
extern "C" int art3m1s_recovery_wind(uint64_t id,const float parameters[5]) {
 Lock lock;
 if (!on_owner()) return 0;
 finish_pending();
 Instance *instance = find(id);
 if (!instance) return 0;
 Selection select(*instance);
 if (parameters) return model_controls_start_wind(parameters[0],parameters[1],parameters[2],
                                                  parameters[3],parameters[4],effective_scale(*instance,instance->scale.value[0]));
 model_controls_stop_wind();
 return 1;
}
static int apply_rotation(Instance *instance,float radians,float frames,float easing){
 float normalized=std::fmod(radians,full_turn);
 if(normalized<0)normalized+=full_turn;
 try{instance->rotation.set({normalized},frames,easing,instance->queuing);}
 catch(const std::bad_alloc&){return 0;}
 model_view_rotation(instance->rotation.value[0]);
 return 1;
}
extern "C" int art3m1s_recovery_set_rotation(uint64_t id,float radians,float frames,float easing) {
 Lock lock;
 if(!on_owner()||!std::isfinite(radians)||!std::isfinite(frames)||!std::isfinite(easing))return 0;
 finish_pending();
 Instance *instance=find(id);
 if(!instance)return 0;
 Selection select(*instance);
 if(!apply_rotation(instance,radians,frames,easing))return 0;
 record_api(*instance,LogOperation::SetRot,{radians,frames,easing,0});
 return 1;
}
extern "C" int art3m1s_recovery_queuing(uint64_t id,int32_t *value,int write) {
 Lock lock;
 if(!on_owner()||!value)return 0;
 finish_pending();
 Instance *instance=find(id);
 if(!instance)return 0;
 if(write)instance->queuing=*value!=0;
 else *value=instance->queuing?1:0;
 return 1;
}
static int apply_color(Instance *instance,uint32_t rgba,float frames,float easing){
 PlayerTransition<4>::Values channels{};
 for(unsigned i=0;i<channels.size();++i)channels[i]=float((rgba>>(24-8*i))&255);
 try{instance->color.set(channels,frames,easing,instance->queuing);}
 catch(const std::bad_alloc&){return 0;}
 model_view_color(packed_player_color(instance->color.value));
 return 1;
}
extern "C" int art3m1s_recovery_set_color(uint64_t id,uint32_t rgba,float frames,float easing) {
 Lock lock;
 if(!on_owner()||!std::isfinite(frames)||!std::isfinite(easing))return 0;
 finish_pending();
 Instance *instance=find(id);
 if(!instance)return 0;
 Selection select(*instance);
 if(!apply_color(instance,rgba,frames,easing))return 0;
 record_api(*instance,LogOperation::SetColor,{frames,easing,0,0},rgba);
 return 1;
}
extern "C" int art3m1s_recovery_get_color(uint64_t id,uint32_t *rgba) {
 Lock lock;
 if(!on_owner()||!rgba)return 0;
 finish_pending();
 const Instance *instance=find(id);
 if(!instance)return 0;
 *rgba=packed_player_color(instance->color.value);
 return 1;
}
extern "C" const char *art3m1s_recovery_variable_label(uint64_t id,uint32_t index){
 Lock lock;if(!on_owner())return nullptr;finish_pending();Instance *i=find(id);if(!i)return nullptr;Selection select(*i);return timeline_variable_label(index);
}
static int apply_variable_diff(const char *key,const char *label,float value,float frames,float easing){
 if(!timeline_set_difference(key,label,value,frames,easing))return 0;model_controls_step(0);return 1;
}
extern "C" int art3m1s_recovery_variable_diff(uint64_t id,const char *key,const char *label,float value,float frames,float easing){
 Lock lock;if(!on_owner())return 0;finish_pending();Instance *i=find(id);if(!i)return 0;Selection select(*i);
 if(!apply_variable_diff(key,label,value,frames,easing))return 0;
 record_api(*i,LogOperation::SetVariableDiff,{value,frames,easing,0},0,label,key);
 return 1;
}
#ifndef DIRECT_RECOVERED_FREEZE_PROBE
static void replay_entry(Instance *instance,const emote_api_log::Entry &entry){
 const auto *label=entry.label.c_str();
 const auto *args=entry.args.data();
 using Op=LogOperation;
 switch(entry.op){
 case Op::SetScale:apply_command(instance,1,label,args,0);break;
 case Op::SetCoord:apply_command(instance,2,label,args,0);break;
 case Op::SetVariable:apply_command(instance,3,label,args,0);break;
 case Op::PlayTimeline:apply_command(instance,4,label,args,entry.flags);break;
 case Op::FadeInTimeline:apply_command(instance,5,label,args,0);break;
 case Op::FadeOutTimeline:apply_command(instance,6,label,args,0);break;
 case Op::StopTimeline:apply_command(instance,7,label,args,0);break;
 case Op::Pass:apply_command(instance,8,label,args,0);break;
 case Op::Step:apply_command(instance,10,label,args,0);break;
 case Op::Skip:apply_command(instance,11,label,args,0);break;
 case Op::SetColor:apply_color(instance,entry.flags,args[0],args[1]);break;
 case Op::SetRot:apply_rotation(instance,args[0],args[1],args[2]);break;
 case Op::SetVariableDiff:apply_variable_diff(entry.key.c_str(),label,args[0],args[1],args[2]);break;
 case Op::SetTimelineBlendRatio:timeline_fade(label,args[0],args[1],args[2],entry.flags!=0);break;
 case Op::SetGrayscale:apply_player_control(instance,2,label,args);break;
 case Op::SetOuterForce:apply_player_control(instance,3,label,args);break;
 case Op::SetOuterRot:apply_player_control(instance,4,label,args);break;
 case Op::Frame:break;
 }
}
#endif
extern "C" int art3m1s_recovery_advance(uint64_t id,float frames){Lock lock;if(!on_owner()||!std::isfinite(frames)||frames<0)return 0;ProfileScope timing(3);collect();Instance *i=find(id);if(!i)return 0;
#ifdef DIRECT_RECOVERED_FREEZE_PROBE
 // Separate diagnostic build only. Keep all geometry/materials and capture
 // evaluation intact while holding timeline, blink and physics clocks fixed.
 return 1;
#else
 Selection select(*i);
 if(i->api_log.replaying()){
  try{
   while(auto entry=i->api_log.next()){
    if(entry->op==LogOperation::Frame){frames=std::max(0.f,entry->args[0]);break;}
    replay_entry(i,*entry);
   }
  }catch(const std::bad_alloc&){i->api_log.stop();return 0;}
 }else record_api(*i,LogOperation::Frame,{frames,0,0,0});
 i->rotation.advance(frames);model_view_rotation(i->rotation.value[0]);
 i->color.advance(frames);model_view_color(packed_player_color(i->color.value));
 if(i->scale.active()||i->coord.active()){i->scale.advance(frames);i->coord.advance(frames);project(*i);}
 i->grayscale.advance(frames);
 i->outer_rotation.advance(frames);model_controls_set_outer_rotation(i->outer_rotation.value[0]);
 for(unsigned n=0;n<i->outer_forces.size();++n){i->outer_forces[n].advance(frames);apply_outer_force(*i,n);}
 timeline_step(frames);model_view_motion_step(frames);model_controls_step(frames);return 1;
#endif
}
extern "C" uint64_t art3m1s_recovery_capture(uint64_t id){Lock lock;if(!on_owner())return 0;ProfileScope timing(0);collect();Instance *i=find(id);if(!i)return 0;Selection select(*i);return register_packet(*i,model_view_capture(-1,-1));}
extern "C" void art3m1s_recovery_capture_batch(const uint64_t *ids,uint64_t *out,unsigned count){
 if(!out)return;for(unsigned n=0;n<count;++n)out[n]=0;
 Lock lock;if(!on_owner()||!ids||count>64)return;collect();if(!count)return;
 // Duplicate contexts cannot be evaluated concurrently. Reject the entire batch.
 for(unsigned n=0;n<count;++n)for(unsigned k=0;k<n;++k)if(ids[n]==ids[k])return;
 uint64_t started=profile_now();bool starting=count>1&&!evaluation_pool.attempted;bool parallel=count>1&&evaluation_pool.init();
 if(starting)direct::log("[recovered-evaluation-workers] ready=%d count=2 joined_batches=1 owner_only_packets=1 geometry_pipeline=1",parallel?1:0);
 for(unsigned base=0;base<count;base+=capture_capacity){Evaluation jobs[capture_capacity]{};unsigned used=count-base<capture_capacity?count-base:capture_capacity;
  for(unsigned n=0;n<used;++n)jobs[n].instance=find(ids[base+n]);
  if(parallel){EvaluationPipeline batch;unsigned positions[capture_capacity]{};
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
  for(unsigned n=0;n<used;++n)if(jobs[n].valid){Selection select(*jobs[n].instance);out[base+n]=register_packet(*jobs[n].instance,model_view_capture_evaluated());}
 }
 profile[0]+=profile_now()-started;profile[4]+=count;
}
extern "C" int art3m1s_recovery_capture_begin(const uint64_t *ids,uint64_t *out,unsigned count){
 if(!out)return 0;for(unsigned n=0;n<count;++n)out[n]=0;
 Lock lock;if(!on_owner()||!ids||count<1||count>capture_capacity)return 0;collect();
 Instance *models[capture_capacity]{};
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
 if(count>=3)evaluate_next_model(pending_capture->batch);
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
extern "C" int art3m1s_recovery_packet_draw(uint64_t id,const float affine[6],float opacity,const float *clip){Lock lock;if(!on_owner())return 0;finish_pending();ProfileScope timing(1);auto p=packets.find(id);if(p==packets.end()||p->second.retired||!p->second.packet)return 0;const auto &packet=p->second;return art3m1s_gxm_draw_recovered_packet(packet.packet,affine,opacity,clip,packet.grayscale,packet.smoothing);}

extern "C" int art3m1s_recovery_shutdown(){Lock lock;if(!on_owner())return 0;finish_pending();evaluation_pool.shutdown();for(auto &p:packets)p.second.retired=true;for(auto &i:instances)i.second->retired=true;collect();owner_set=false;return 1;}

extern "C" void art3m1s_recovery_take_pose_profile(uint64_t out[6]){Lock lock;if(!out)return;std::memset(out,0,6*sizeof(*out));if(on_owner())model_view_take_pose_profile(out);}

extern "C" void art3m1s_recovery_take_dirty_profile(uint64_t out[7]){Lock lock;if(!out)return;std::memset(out,0,7*sizeof(*out));if(on_owner())model_view_take_dirty_profile(out);}
extern "C" void art3m1s_recovery_take_pose_parts(uint64_t out[10]){Lock lock;if(!out)return;std::memset(out,0,10*sizeof(*out));if(on_owner())for(auto &entry:instances){Selection select(*entry.second);uint64_t part[10];model_view_take_pose_parts(part);for(unsigned n=0;n<10;++n)out[n]+=part[n];}}

extern "C" void art3m1s_recovery_take_packet_profile(uint64_t out[8]){Lock lock;if(!out)return;std::memset(out,0,8*sizeof(*out));if(on_owner())em_render_packet_take_profile(out);}

extern "C" void art3m1s_recovery_take_vertex_parts(uint64_t out[3]){Lock lock;if(!out)return;std::memset(out,0,3*sizeof(*out));if(on_owner())model_view_take_vertex_parts(out);}
