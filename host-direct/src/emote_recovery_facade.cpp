#include "emote_recovery_facade.h"
#include <atomic>
#include <map>
#include <memory>
#include <vector>
#include <cstring>
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
namespace {
std::atomic_flag mutex=ATOMIC_FLAG_INIT;
struct Lock {Lock(){while(mutex.test_and_set(std::memory_order_acquire)) {}}~Lock(){mutex.clear(std::memory_order_release);}};
ThreadId owner{};bool owner_set=false;uint64_t next_id=0;
bool on_owner(){return owner_set&&owner==thread_id();}
uint64_t profile[6]{};
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
struct Instance {
 std::vector<uint8_t> bytes;EmPsb psb{};EmPsbValue root{};
 EmTimelineContext *timeline=nullptr;EmControlsContext *controls=nullptr;EmViewContext *view=nullptr;
 float center_x=480,center_y=272;
 float scale=1,x=0,y=0;bool retired=false;
 ~Instance(){if(view)model_view_context_destroy(view);if(controls)model_controls_context_destroy(controls);if(timeline)timeline_context_destroy(timeline);}
};
struct Packet {EmRenderPacket *packet=nullptr;bool retired=false;};
auto &instances=*new std::map<uint64_t,std::unique_ptr<Instance>>;
auto &packets=*new std::map<uint64_t,Packet>;
struct Selection {
 EmTimelineContext *time;EmControlsContext *controls;EmViewContext *view;
 Selection(Instance &i):time(timeline_context_use(i.timeline)),controls(model_controls_context_use(i.controls)),view(model_view_context_use(i.view)){}
 ~Selection(){model_view_context_use(view);model_controls_context_use(controls);timeline_context_use(time);}
};
void collect(){for(auto i=packets.begin();i!=packets.end();)if(i->second.retired){em_render_packet_destroy(i->second.packet);i=packets.erase(i);}else ++i;
 for(auto i=instances.begin();i!=instances.end();)if(i->second->retired)i=instances.erase(i);else ++i;}
Instance *find(uint64_t id){auto i=instances.find(id);return i==instances.end()||i->second->retired?nullptr:i->second.get();}
uint64_t token(){return next_id==std::numeric_limits<uint64_t>::max()?0:++next_id;}
bool header(const uint8_t *p,size_t n){if(!p||n<40||n>UINT32_MAX||memcmp(p,"PSB\0",4))return false;
 uint16_t version;memcpy(&version,p+4,2);if(version<2||version>4||(version==4&&n<56))return false;
 for(unsigned o=8;o<40;o+=4){uint32_t v;memcpy(&v,p+o,4);if(v>=n)return false;}return true;}
}
extern "C" uint64_t art3m1s_recovery_create(const char *path,const uint8_t *data,size_t size,uint32_t width,uint32_t height){
 Lock lock;if(owner_set&&!on_owner())return 0;if(!width||!height||width>16384||height>16384||!header(data,size))return 0;
 if(!owner_set){owner=thread_id();owner_set=true;}collect();
 try {auto i=std::unique_ptr<Instance>(new Instance);i->center_x=width*.5f;i->center_y=height*.5f;i->bytes.assign(data,data+size);i->psb.raw=i->bytes.data();i->psb.length=(uint32_t)size;em_psb_init_header(&i->psb);em_psb_root(&i->root,&i->psb);
  i->timeline=timeline_context_create();i->controls=model_controls_context_create();i->view=model_view_context_create();if(!i->timeline||!i->controls||!i->view)return 0;
  {Selection select(*i);timeline_bind_neutral(i->root);model_controls_bind(i->root);
   const EmTextureHost *host=art3m1s_gxm_recovered_texture_host();if(!host||!model_view_texture_host(host))return 0;
   // No filename-based provenance inference: unknown profile stays zero.
   model_view_material_profile(0);if(!model_view_load(i->root))return 0;
   if(!model_view_projection(1,i->center_x,i->center_y))return 0;}
  (void)path;uint64_t id=token();if(!id)return 0;instances.emplace(id,std::move(i));return id;
 }catch(...){return 0;}
}
extern "C" void art3m1s_recovery_destroy(uint64_t id){Lock lock;auto i=instances.find(id);if(i!=instances.end())i->second->retired=true;}
extern "C" int art3m1s_recovery_collect(){Lock lock;if(!on_owner())return 0;collect();return 1;}
extern "C" void art3m1s_recovery_take_profile(uint64_t values[6]){Lock lock;if(!values)return;std::memcpy(values,profile,sizeof profile);std::memset(profile,0,sizeof profile);}
extern "C" int art3m1s_recovery_prepare_resources(){Lock lock;if(!owner_set)return 1;if(!on_owner())return -1;ProfileScope timing(2);collect();int result=1;
 for(auto &entry:instances){Selection select(*entry.second);int ready=model_view_prepare_host_textures();if(ready<0)return -1;if(!ready)result=0;}
 // A transition can retain a packet after its model/view was destroyed.
 for(auto &entry:packets){int ready=art3m1s_gxm_prepare_recovered_packet(entry.second.packet);if(ready<0)return -1;if(!ready)result=0;}
 return result;}
extern "C" int art3m1s_recovery_command(uint64_t id,uint32_t op,const char *label,const float args[4],uint32_t flags){
 Lock lock;if(!on_owner()||!args)return 0;Instance *i=find(id);if(!i)return 0;for(unsigned k=0;k<4;++k)if(!std::isfinite(args[k]))return 0;
 Selection select(*i);switch(op){
 case 1:if(args[1]!=0||args[2]!=0)return 0;if(!model_view_projection(args[0],i->center_x+i->x,i->center_y+i->y))return 0;i->scale=args[0];return 1;
 case 2:if(args[2]!=0||args[3]!=0)return 0;if(!model_view_projection(i->scale,i->center_x+args[0],i->center_y+args[1]))return 0;i->x=args[0];i->y=args[1];return 1;
 case 3:return label&&timeline_set_variable(label,args[0],args[1],args[2],0);
 case 4:return label&&timeline_play(label,flags);
 case 5:return label&&timeline_fade_in(label,args[0],args[1]);
 case 6:return label&&timeline_fade(label,0,args[0],args[1],1);
 case 7:return label&&timeline_stop(label);
 case 8:timeline_pass();return 1;
 case 9:return args[0]==1; // Current recovered tessellation is fixed.
 default:return 0;}}
extern "C" int art3m1s_recovery_query(uint64_t id,uint32_t op,const char *label,float *out){Lock lock;if(!on_owner()||!out)return 0;Instance *i=find(id);if(!i)return 0;Selection select(*i);
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
extern "C" uint64_t art3m1s_recovery_capture(uint64_t id){Lock lock;if(!on_owner())return 0;ProfileScope timing(0);collect();Instance *i=find(id);if(!i)return 0;Selection select(*i);EmRenderPacket *p=model_view_capture(-1,-1);if(!p)return 0;
 try{uint64_t t=token();if(!t){em_render_packet_destroy(p);return 0;}packets.emplace(t,Packet{p,false});return t;}catch(...){em_render_packet_destroy(p);return 0;}}
extern "C" void art3m1s_recovery_packet_release(uint64_t id){Lock lock;auto p=packets.find(id);if(p!=packets.end())p->second.retired=true;}
extern "C" int art3m1s_recovery_packet_prepare(uint64_t id){Lock lock;if(!on_owner())return -1;collect();auto p=packets.find(id);if(p==packets.end()||p->second.retired)return -1;return art3m1s_gxm_prepare_recovered_packet(p->second.packet);}
// Neutral parent elimination is valid only for source-over packet materials.
// Additive/subtractive/destination-dependent meshes must retain isolation.
extern "C" int art3m1s_recovery_packet_source_over(uint64_t id){Lock lock;if(!on_owner())return 0;auto p=packets.find(id);if(p==packets.end()||p->second.retired)return 0;
 uint32_t count=0;const EmRenderCommand *commands=em_render_packet_commands(p->second.packet,&count);
 for(uint32_t n=0;n<count;++n)if(commands[n].op==EM_RENDER_MATERIAL){uint32_t m=uint32_t(commands[n].args[0]);if(m!=UINT32_MAX&&m!=0&&m!=16)return 0;}
 return 1;}
extern "C" int art3m1s_recovery_packet_draw(uint64_t id,const float affine[6],float opacity,const float *clip){Lock lock;if(!on_owner())return 0;ProfileScope timing(1);auto p=packets.find(id);if(p==packets.end()||p->second.retired)return 0;return art3m1s_gxm_draw_recovered_packet(p->second.packet,affine,opacity,clip);}

extern "C" int art3m1s_recovery_shutdown(){Lock lock;if(!on_owner())return 0;for(auto &p:packets)p.second.retired=true;for(auto &i:instances)i.second->retired=true;collect();owner_set=false;return 1;}
