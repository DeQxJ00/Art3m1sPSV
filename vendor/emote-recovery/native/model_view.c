/* New GXM display adapter for binary-derived PSB scene fields.
 * NOT a claim that the complete original GXM renderer has been recovered.
 * Evidence: 8118B3D6/811A72AC textures, 8118CC88 icons, 81190FFE layers,
 * 81192C3E parameter mapping, 8119AC18/81199F7E frame fields.
 * Scope: timeline-driven poses, affine layers, referenced motions and patches.
 */
#include "model_view.h"
#include "exact_bytes.h"
#include "patch_ops.h"
#include "append_log.h"
#include "parameter.h"
#include "mesh.h"
#include "mesh_child.h"
#include "pose_update.h"
#include "material_colors.h"
#include "timeline.h"
#include "model_controls.h"
#include "physics_lookup.h"
#include "gpu.h"
#include "render_packet.h"
#include "scene_runtime.h"
#include "motion_clock.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/threadmgr/thread.h>

#ifndef EM_VERTEX_WORKERS
#if defined(__vita__) || defined(EM_VERTEX_THREADS_HOST)
#define EM_VERTEX_WORKERS 1
#else
#define EM_VERTEX_WORKERS 0
#endif
#endif
#if EM_VERTEX_WORKERS
#include <pthread.h>
#if defined(EM_VERTEX_THREADS_HOST)
#include <semaphore.h>
#include <errno.h>
#include <fenv.h>
#else
#include <psp2/kernel/threadmgr/semaphore.h>
#endif
#endif

#ifndef EM_PROFILE_DRAW
#define EM_PROFILE_DRAW 0
#endif
typedef EmPsbValue V;
typedef struct SharedTexture SharedTexture;
typedef struct Texture {char name[128];V icons;int w,h;uint32_t *rgba;GpuTexture gpu;
 const uint8_t *source_pixels,*source_palette;unsigned source_size,palette_size;
 struct Texture *share_next;SharedTexture *shared;
} Texture;
/* Serial host resource cache, not an inferred original-engine data structure.
 * Keys borrow bytes only from live owners; removing one owner leaves another
 * as the comparison source. Pending upload data separately owns a copy or
 * retains an explicit backing lease after the last view owner is gone. */
struct SharedTexture {SharedTexture *next;Texture *owners;GpuTexture gpu;int kind,w,h;uint64_t hash,identity;unsigned refs;EmTextureHost host;EmTextureSource source;uint8_t *owned_source;EmTextureSourceLease source_lease;uint64_t host_handle;int host_error;};
static SharedTexture *shared_textures;
static uint64_t next_texture_identity;
static EmRenderPacket *capture_packet;
static EmAppendLog performance_log,draw_profile_log;
void model_view_log_stats(long long *time_us,unsigned *opens,unsigned *errors){
 if(time_us)*time_us=performance_log.total_us+draw_profile_log.total_us;
 if(opens)*opens=performance_log.opens+draw_profile_log.opens;
 if(errors)*errors=performance_log.errors+draw_profile_log.errors;
}
typedef struct {float a,b,c,d,x,y,alpha;int visible;float z;int chain;float angle,zx,zy,shx,shy;int fx,fy;uint32_t tint;unsigned clip;unsigned opacity;int independent_inherit;} Mat;
typedef struct {const EmSceneLayer *layer;const EmSceneContent *content;Mat m;int type;float time;uint32_t colors[4];int own_deform,child_chain,active,layer_index,reference_emitted;unsigned child_clip;} Node;
typedef struct {const EmSceneLayer *layer;Node targets[33];unsigned count,ancestor,first_sprite,draw_count;int depth,used,owner_event;} MaskGroup;
typedef struct {Texture *t;Mat m;float left,top,w,h;int attr;float ox,oy;uint32_t colors[4];unsigned stencil_write,blend_mode;int original_material;} Sprite;
typedef struct {Mat m;float w,h,ox,oy,inv_det,inv_w,inv_h;EmPoint cp[16],raw_cp[16];EmWorldPatch patch;int next,grid_bounded;} Deform;
typedef struct {Sprite key;GpuVertex grid[81];float scale,x,y;int width,height,valid;unsigned epoch;} MeshCache;
#define ANCHOR_INSTANCES 1024
#define ANCHOR_CANDIDATES 8192
typedef struct {const char *label;float x,y;int valid;} AnchorCandidate;
typedef struct {int parent,layer;unsigned first,count;} AnchorInstance;
typedef struct {EmMotionEventCursor cursor;float time;} ViewEventLayer;
typedef struct {int parent,layer;EmSceneMotion *motion;const EmSceneContent *source_key;unsigned epoch,seek_serial;float time,raw_time;EmMotionEventCursor tag;ViewEventLayer *layers;EmMotionClock clock;} ViewEventInstance;
typedef struct {Mat pose;float ratios[5];uint32_t colors[4];unsigned opacity;} LocalPoseValue;
typedef struct {const EmSceneFrame *frame,*next;float ratio;unsigned seek;LocalPoseValue value;} LocalPoseMemo;
typedef struct {const EmSceneLayer *layer;unsigned count;float values[16];EmPoint points[16];int active;} PatchMemo;
typedef struct {const EmSceneLayer *layer;EmMeshChildPose input,output;EmPoint patch[16],offset;int w,h;unsigned sync,inherit;} ParentSyncMemo;
typedef struct {const EmSceneLayer *layer;int needed;} AnchorDemandMemo;
typedef struct {uint32_t bits,valid;float co,si;} ShapeTrigMemo;
typedef struct {uint32_t key[11];float result[4];int valid;} ShapeMemo;
struct EmViewContext {
 EmControlsContext *controls_for_pose;
 unsigned anchor_binding;AnchorDemandMemo anchor_demand[512];
 ShapeTrigMemo shape_trig_memo[8];ShapeMemo shape_memo[64];
 uint64_t pose_parts_data[10];int pose_parts_active_data;
 int evaluation_ready,visible_sprites_data;SceInt64 evaluation_us;
 int vertices_ready,vertices_width,vertices_height;EmViewVertexStats prepared_vertices;
 PatchMemo patch_memo[64];
 ParentSyncMemo parent_sync[128];
 LocalPoseMemo *local_poses;unsigned local_pose_count;
 V root_data;
 EmScene scene_data;
 unsigned scene_epoch_data;
 Texture textures_data [32];
 int texture_count_data;
 char status_data [128];
 float fit_scale_data;
 float fit_x_data;
 float fit_y_data;
 int fitted_data;
 Node node_stack_data [9][512];
 MaskGroup mask_groups_data [128];
 int mask_mode_data,mask_failed_data;
 unsigned mask_count_data;
 unsigned mask_write_data;
 int generating_masks_data;
 Sprite sprites_data [1024];
 int sprite_count_data;
 int missing_data;
 int nonneutral_data;
 Deform deforms_data [1024];
 int deform_count_data;
 int old_deform_count_data;
 unsigned char deform_changed_data [1024];
 MeshCache mesh_cache_data [1024];
 float eye_value_data;
 float mouth_value_data;
 unsigned samples_data;
 SceInt64 eval_sum_data;
 SceInt64 draw_sum_data;
 float box_x,box_y,box_w,box_h;
 EmTextureHost texture_host;
 EmTextureSourceLease texture_source_lease;
 int projection_explicit;
 float projection_scale,projection_x,projection_y;
 AnchorCandidate anchor_candidates[ANCHOR_CANDIDATES];
 AnchorInstance anchor_instances[ANCHOR_INSTANCES];
 EmPhysicsLookupNode anchor_nodes[ANCHOR_INSTANCES];
 unsigned anchor_children[ANCHOR_INSTANCES],anchor_order[ANCHOR_INSTANCES];
 unsigned anchor_instance_count,anchor_candidate_count;
 int anchor_instance,anchor_failed,anchor_only;
 int material_profile;unsigned grid_steps_data;
 float motion_clock;
 unsigned event_seek_serial;
 int event_instance,event_failed;
 ViewEventInstance *event_instances;
 unsigned event_instance_count,event_instance_capacity;
 EmMotionEventQueue event_queues[9],event_output;
 size_t event_read;
};
static EmViewContext default_view;
static __thread EmViewContext *current_view=&default_view;
#define root (current_view->root_data)
#define scene (current_view->scene_data)
#define scene_epoch (current_view->scene_epoch_data)
#define textures (current_view->textures_data)
#define texture_count (current_view->texture_count_data)
#define status (current_view->status_data)
#define fit_scale (current_view->fit_scale_data)
#define fit_x (current_view->fit_x_data)
#define fit_y (current_view->fit_y_data)
#define fitted (current_view->fitted_data)
#define node_stack (current_view->node_stack_data)
#define mask_groups (current_view->mask_groups_data)
#define view_mask_count (current_view->mask_count_data)
#define mask_write (current_view->mask_write_data)
#define generating_masks (current_view->generating_masks_data)
#define sprites (current_view->sprites_data)
#define sprite_count (current_view->sprite_count_data)
#define missing (current_view->missing_data)
#define nonneutral (current_view->nonneutral_data)
#define deforms (current_view->deforms_data)
#define deform_count (current_view->deform_count_data)
#define old_deform_count (current_view->old_deform_count_data)
#define deform_changed (current_view->deform_changed_data)
#define mesh_cache (current_view->mesh_cache_data)
#define eye_value (current_view->eye_value_data)
#define mouth_value (current_view->mouth_value_data)
#define samples (current_view->samples_data)
#define eval_sum (current_view->eval_sum_data)
#define draw_sum (current_view->draw_sum_data)
EmViewContext *model_view_context_create(void){return calloc(1,sizeof(EmViewContext));}
EmViewContext *model_view_context_use(EmViewContext *context){EmViewContext *previous=current_view;current_view=context?context:&default_view;return previous;}
void model_view_context_destroy(EmViewContext *context){if(!context||context==&default_view)return;EmViewContext *previous=model_view_context_use(context);model_view_free();current_view=previous==context?&default_view:previous;free(context);}
int model_view_projection(float scale,float x,float y){
 if(!isfinite(scale)||!isfinite(x)||!isfinite(y))return 0;
 current_view->projection_scale=scale;current_view->projection_x=x;current_view->projection_y=y;current_view->projection_explicit=1;return 1;
}
void model_view_layout(float x,float y,float w,float h){if(!current_view->projection_explicit&&current_view->box_x==x&&current_view->box_y==y&&current_view->box_w==w&&current_view->box_h==h)return;current_view->projection_explicit=0;current_view->box_x=x;current_view->box_y=y;current_view->box_w=w;current_view->box_h=h;fitted=0;}

/* PSB helpers below are used only while loading atlases. */
static V get(V v,const char *s){V r={0};if(v.data&&*v.data==33)em_psb_find(&v,(const uint8_t*)s,&r);return r;}
static float num(V v,float def){return v.data&&*v.data!=1?em_psb_float(&v):def;}
static unsigned count(V v){return v.data&&(*v.data==32||*v.data==33)?em_psb_array(v.data+1).count:0;}
static float prop(V v,const char *s,float def){return num(get(v,s),def);}
static const char *str(V v){return v.data&&*v.data>=21&&*v.data<=24?em_psb_string(&v):"";}
static void reset_mesh_cache(void);
static void vertex_pool_stop(void);
static unsigned grid_steps(void){return current_view->grid_steps_data?current_view->grid_steps_data:8;}
static unsigned sprite_grid_steps(const Sprite *s){return s->m.chain<0?1:grid_steps();}
static unsigned sprite_grid_vertices(const Sprite *s){unsigned side=sprite_grid_steps(s)+1;return side*side;}
int model_view_set_grid_steps(unsigned steps){
 if(steps!=1&&steps!=2&&steps!=4&&steps!=6&&steps!=8)return 0;
 if(grid_steps()!=steps){current_view->grid_steps_data=steps;reset_mesh_cache();current_view->vertices_ready=0;current_view->evaluation_ready=0;}
 return 1;
}
void model_view_material_profile(int profile){if(profile!=current_view->material_profile){current_view->material_profile=profile;if(profile!=1)current_view->mask_mode_data=0;reset_mesh_cache();}}

/* Platform texture conversion. BC3 is a standard GPU format, not E-mote code.
 * Khronos Data Format 1.4, S3TC. Vita Morton order uses Y in the even bits. */
static unsigned morton(unsigned x,unsigned y,unsigned w,unsigned h){
 unsigned r=0,b=0;while(w>1&&h>1){r|=(y&1)<<b++;r|=(x&1)<<b++;x>>=1;y>>=1;w>>=1;h>>=1;}return r|((w>1?x:y)<<b);
}
static uint32_t rgb565(unsigned c){unsigned r=(c>>11)&31,g=(c>>5)&63,b=c&31;return ((r<<3)|(r>>2))|(((g<<2)|(g>>4))<<8)|(((b<<3)|(b>>2))<<16);}
static void bc3(Texture *t,const uint8_t *p,int linear){
 unsigned bw=(t->w+3)/4,bh=(t->h+3)/4;
 for(unsigned y=0;y<bh;++y)for(unsigned x=0;x<bw;++x){
  const uint8_t *b=p+16*(linear?y*bw+x:morton(x,y,bw,bh));unsigned a[8]={b[0],b[1]};
  if(a[0]>a[1])for(int j=1;j<=6;++j)a[j+1]=((7-j)*a[0]+j*a[1])/7;
  else{for(int j=1;j<=4;++j)a[j+1]=((5-j)*a[0]+j*a[1])/5;a[6]=0;a[7]=255;}
  uint64_t ai=0;for(int j=0;j<6;++j)ai|=(uint64_t)b[j+2]<<(8*j);
  uint32_t c[4]={rgb565(b[8]|b[9]<<8),rgb565(b[10]|b[11]<<8),0,0};
  for(int j=0;j<3;++j){unsigned u=(c[0]>>(8*j))&255,v=(c[1]>>(8*j))&255;c[2]|=((2*u+v)/3)<<(8*j);c[3]|=((u+2*v)/3)<<(8*j);}
  uint32_t ci=(uint32_t)b[12]|(uint32_t)b[13]<<8|(uint32_t)b[14]<<16|(uint32_t)b[15]<<24;
  for(int j=0;j<16;++j){unsigned px=x*4+j%4,py=y*4+j/4;if(px<(unsigned)t->w&&py<(unsigned)t->h)t->rgba[py*t->w+px]=c[(ci>>(2*j))&3]|a[(ai>>(3*j))&7]<<24;}
 }
}
static uint64_t texture_hash(const Texture *t){
 /* This is only a rejection filter, never a texture identity. Every hit
  * below still requires complete pixel AND palette byte equality. Reading
  * every byte here duplicated that check and stalled ARM32 model switches. */
 uint32_t h=2166136261u;
 if(t->source_size<=256){
  for(unsigned i=0;i<t->source_size;++i)h=(h^t->source_pixels[i])*16777619u;
 }else{
  unsigned last=t->source_size-8,stride=last/31;
  for(unsigned sample=0;sample<32;++sample){unsigned offset=sample==31?last:sample*stride;
   for(unsigned j=0;j<8;++j)h=(h^t->source_pixels[offset+j])*16777619u;
  }
 }
 for(unsigned i=0;i<t->palette_size;++i)h=(h^t->source_palette[i])*16777619u;
 return h;
}
static void texture_lease_retain(void *object){SharedTexture *e=object;if(e->refs==UINT32_MAX)abort();++e->refs;}
static void texture_source_release(SharedTexture *e){
 free(e->owned_source);e->owned_source=NULL;e->source.data=NULL;
 EmTextureSourceLease lease=e->source_lease;e->source_lease=(EmTextureSourceLease){0};
 if(lease.release)lease.release(lease.object);
}
static void texture_lease_release(void *object){
 SharedTexture *e=object;if(!e->refs)abort();if(--e->refs)return;
 SharedTexture **link=&shared_textures;while(*link&&*link!=e)link=&(*link)->next;if(*link)*link=e->next;if(e->host.upload){if(e->host_handle)e->host.release(e->host.user,e->host_handle);texture_source_release(e);}else gpu_texture_free(&e->gpu);free(e);
}
static void *texture_lease_gpu(void *object){SharedTexture *e=object;return e->host.upload?NULL:&e->gpu;}
static int texture_lease_prepare(void *object,uint64_t *handle){
 SharedTexture *e=object;*handle=0;if(!e->host.upload||e->host_error)return -1;
 if(e->host_handle){*handle=e->host_handle;return 1;}
 uint64_t h=0;int r=e->host.upload(e->host.user,&e->source,&h);
 if(r==1&&h){e->host_handle=h;*handle=h;texture_source_release(e);return 1;}
 if(h)e->host.release(e->host.user,h);
 if(r!=0||h){e->host_error=1;return -1;}return 0;
}
int model_view_texture_host(const EmTextureHost *host){
 if(texture_count||(host&&(!host->upload||!host->release)))return 0;
 current_view->texture_host=host?*host:(EmTextureHost){0};return 1;
}
int model_view_texture_source_lease(const EmTextureSourceLease *lease){
 if(texture_count||(lease&&(!lease->object||!lease->retain||!lease->release)))return 0;
 current_view->texture_source_lease=lease?*lease:(EmTextureSourceLease){0};return 1;
}
static void texture_release(Texture *t){
 SharedTexture *e=t->shared;
 if(e){Texture **owner=&e->owners;while(*owner&&*owner!=t)owner=&(*owner)->share_next;if(*owner)*owner=t->share_next;texture_lease_release(e);}
 else gpu_texture_free(&t->gpu);
 t->shared=NULL;t->share_next=NULL;memset(&t->gpu,0,sizeof(t->gpu));
}
static int texture_acquire(Texture *t,int kind){
 int width=t->w,height=t->h;uint64_t hash=texture_hash(t);
 for(SharedTexture *e=shared_textures;e;e=e->next){Texture *owner=e->owners;
  if(e->host.user!=current_view->texture_host.user||e->host.upload!=current_view->texture_host.upload||e->host.release!=current_view->texture_host.release)continue;
  if(!owner||e->kind!=kind||e->w!=width||e->h!=height||e->hash!=hash||owner->source_size!=t->source_size||owner->palette_size!=t->palette_size)continue;
  /* Equal addresses and lengths are exactly equal bytes; distinct backing
   * allocations still need complete comparison, even on fingerprint hits. */
  if((owner->source_pixels!=t->source_pixels&&memcmp(owner->source_pixels,t->source_pixels,t->source_size))||
     (t->palette_size&&owner->source_palette!=t->source_palette&&memcmp(owner->source_palette,t->source_palette,t->palette_size)))continue;
  t->gpu=e->gpu;t->w=e->host.upload?e->source.width:t->gpu.width;t->h=e->host.upload?e->source.height:t->gpu.height;t->shared=e;t->share_next=e->owners;e->owners=t;texture_lease_retain(e);return 1;
 }
 if(current_view->texture_host.upload){
  int sw=kind==2||kind==3;if(kind==2){int pw=4,ph=4;while(pw<t->w)pw*=2;while(ph<t->h)ph*=2;t->w=pw;t->h=ph;}
  uint32_t bytes=kind==4?(uint32_t)t->w*t->h*4:((uint32_t)t->w+3)/4*(((uint32_t)t->h+3)/4)*16;
  if(kind!=4&&t->source_size<bytes)return 0;
  SharedTexture *e=calloc(1,sizeof(*e));if(!e)return 0;
  const uint8_t *source_data=t->source_pixels;
  if(kind!=4&&current_view->texture_source_lease.object){
   e->source_lease=current_view->texture_source_lease;e->source_lease.retain(e->source_lease.object);
  }else{
   e->owned_source=malloc(bytes);if(!e->owned_source){free(e);return 0;}
   if(kind==4){for(int y=0;y<t->h;++y)for(int x=0;x<t->w;++x){unsigned i=morton(x,y,t->w,t->h);if(i>=t->source_size){free(e->owned_source);free(e);return 0;}memcpy(e->owned_source+4*(y*t->w+x),t->source_palette+4*t->source_pixels[i],4);}}
   else memcpy(e->owned_source,t->source_pixels,bytes);
   source_data=e->owned_source;
  }
  e->next=shared_textures;e->owners=t;e->kind=kind;e->w=width;e->h=height;e->hash=hash;e->identity=++next_texture_identity;e->refs=1;e->host=current_view->texture_host;
  e->source=(EmTextureSource){e->identity,kind==4?EM_TEXTURE_RGBA8:EM_TEXTURE_BC3,sw?EM_TEXTURE_SWIZZLED:EM_TEXTURE_LINEAR,(uint32_t)width,(uint32_t)height,(uint32_t)t->w,(uint32_t)t->h,bytes,source_data};
  shared_textures=e;t->shared=e;return 1;
 }
 /* Retain compressed blocks on native BC3 paths; CI8 alone expands to RGBA. */
 if(kind==2){int pw=4,ph=4;while(pw<t->w)pw*=2;while(ph<t->h)ph*=2;
  if(gpu_texture_bc3_swizzled(&t->gpu,pw,ph,t->source_pixels,t->source_size)<0)return 0;t->w=pw;t->h=ph;
 }else if(kind==1){if(gpu_texture_bc3_linear(&t->gpu,t->w,t->h,t->source_pixels,t->source_size)<0)return 0;t->w=t->gpu.width;t->h=t->gpu.height;
 }else if(kind!=3||gpu_texture_bc3_swizzled(&t->gpu,t->w,t->h,t->source_pixels,t->source_size)<0){
  t->rgba=malloc(t->w*t->h*4);if(!t->rgba)return 0;
  if(kind==3)bc3(t,t->source_pixels,0);
  else for(int y=0;y<t->h;++y)for(int x=0;x<t->w;++x){const uint8_t *c=t->source_palette+4*t->source_pixels[morton(x,y,t->w,t->h)];t->rgba[y*t->w+x]=(uint32_t)c[0]|(uint32_t)c[1]<<8|(uint32_t)c[2]<<16|(uint32_t)c[3]<<24;}
  if(gpu_texture(&t->gpu,t->w,t->h,t->rgba)<0)return 0;free(t->rgba);t->rgba=NULL;
 }
 SharedTexture *e=calloc(1,sizeof(*e));if(!e)return 0;
 *e=(SharedTexture){.next=shared_textures,.owners=t,.gpu=t->gpu,.kind=kind,.w=width,.h=height,.hash=hash,.identity=++next_texture_identity,.refs=1};shared_textures=e;t->shared=e;return 1;
}
/* Render owner, outside the host scene. Does not evaluate or advance a pose. */
int model_view_prepare_host_textures(void){
 int ready_status=1;
 for(int i=0;i<texture_count;++i){SharedTexture *e=textures[i].shared;if(!e||!e->host.upload)continue;
  uint64_t handle=0;int ready=texture_lease_prepare(e,&handle);if(ready<0)return -1;if(!ready)ready_status=0;
 }
 return ready_status;
}
void model_view_texture_stats(unsigned *resources,unsigned *owners){
 unsigned r=0,o=0;for(SharedTexture *e=shared_textures;e;e=e->next){++r;for(Texture *t=e->owners;t;t=t->share_next)++o;}
 if(resources)*resources=r;if(owners)*owners=o;
}
static void event_free(void){
 for(unsigned i=0;i<current_view->event_instance_count;++i)free(current_view->event_instances[i].layers);
 free(current_view->event_instances);current_view->event_instances=NULL;current_view->event_instance_count=current_view->event_instance_capacity=0;
 for(unsigned i=0;i<9;++i){free(current_view->event_queues[i].records);current_view->event_queues[i]=(EmMotionEventQueue){0};}
 free(current_view->event_output.records);current_view->event_output=(EmMotionEventQueue){0};current_view->event_read=0;
 current_view->event_instance=-1;current_view->event_failed=0;current_view->motion_clock=0;current_view->event_seek_serial=0;
}
void model_view_motion_step(float frames){if(isfinite(frames)&&isfinite(current_view->motion_clock+frames))current_view->motion_clock+=frames;else current_view->event_failed=1;}
void model_view_motion_seek(float frame){if(!isfinite(frame)){current_view->event_failed=1;return;}current_view->motion_clock=frame;if(!++current_view->event_seek_serial)++current_view->event_seek_serial;}
int model_view_poll_event(EmMotionEvent *out){
 if(!out||current_view->event_failed)return -1;
 EmMotionEventQueue*q=&current_view->event_output;
 if(current_view->event_read==q->count){current_view->event_read=q->count=0;return 0;}
 *out=q->records[current_view->event_read++];return 1;
}
void model_view_free(void){em_log_close(&performance_log,sceKernelGetSystemTimeWide);em_log_close(&draw_profile_log,sceKernelGetSystemTimeWide);vertex_pool_stop();event_free();memset(current_view->parent_sync,0,sizeof current_view->parent_sync);memset(current_view->anchor_demand,0,sizeof current_view->anchor_demand);free(current_view->local_poses);current_view->local_poses=NULL;current_view->local_pose_count=0;memset(current_view->patch_memo,0,sizeof current_view->patch_memo);em_scene_free(&scene);scene_epoch=0;reset_mesh_cache();for(int i=0;i<texture_count;++i){free(textures[i].rgba);textures[i].rgba=NULL;texture_release(textures+i);}texture_count=0;fitted=0;}
static int model_view_load_inner(V v,const EmScene *prototype){
 model_view_free();root=v;model_controls_bind(root);V src=get(root,"source");
 for(unsigned i=0;i<count(src)&&texture_count<32;++i){
  V s;em_psb_object_at(&s,&src,i);V tex=get(s,"texture"),pix=get(tex,"pixel");
  if(!pix.data)continue;Texture *t=textures+texture_count;memset(t,0,sizeof(*t));
  if(!em_psb_object_key(&src,i,t->name,sizeof(t->name)))continue;
  t->w=prop(tex,"width",0);t->h=prop(tex,"height",0);t->icons=get(s,"icon");
  const char *format=str(get(tex,"type"));int linear=!strcmp(format,"DXT5"),padded_sw=!strcmp(format,"DXT5_SWIZZLED"),isbc3=linear||padded_sw||!strcmp(format,"BC3"),ispal=!strcmp(format,"CI8_SW");V pal=get(tex,"pal");
  unsigned required=isbc3?(unsigned)(((t->w+3)/4)*((t->h+3)/4)*16):(unsigned)(t->w*t->h);
  if((!isbc3&&!ispal)||t->w<=0||t->h<=0||t->w>4096||t->h>4096||em_psb_resource_size(&pix)<required||
    (ispal&&(!pal.data||strcmp(str(get(tex,"palType")),"RGBA8")||em_psb_resource_size(&pal)<1024))){
   snprintf(status,sizeof(status),"UNSUPPORTED TEXTURE");model_view_free();return 0;
  }
  ++texture_count;
  t->source_pixels=em_psb_resource(&pix);t->source_size=em_psb_resource_size(&pix);
  t->source_palette=ispal?em_psb_resource(&pal):NULL;t->palette_size=ispal?1024:0;
  if(!texture_acquire(t,padded_sw?2:linear?1:isbc3?3:4)){model_view_free();return 0;}
 }
 const char *names[32];for(int i=0;i<texture_count;++i)names[i]=textures[i].name;
 if(!texture_count||!(prototype?em_scene_clone(&scene,prototype):em_scene_compile(&scene,root,names,texture_count))){model_view_free();snprintf(status,sizeof(status),"SCENE COMPILE FAILED");return 0;}
 for(unsigned i=0;i<scene.motion_count;++i){EmSceneMotion *m=scene.motions+i;
  for(unsigned j=0;j<m->parameter_count;++j)model_controls_range(m->parameters[j].id,m->parameters[j].range.begin,m->parameters[j].range.end);
  for(unsigned j=0;j<m->layer_count;++j)for(unsigned k=0;k<m->layers[j].combinator_count;++k){EmSceneCombinator *c=m->layers[j].combinators+k;model_controls_range(c->id,c->begin,c->end);}
 }
 if(scene.total_layers&&scene.total_layers<=262144/sizeof(LocalPoseMemo)){
  current_view->local_poses=calloc(scene.total_layers,sizeof(LocalPoseMemo));
  if(current_view->local_poses)current_view->local_pose_count=scene.total_layers;
 }
 timeline_filter(model_controls_has);model_controls_step(0);
 snprintf(status,sizeof(status),"%u LAYERS %u KEYS",scene.total_layers,scene.total_frames);return 1;
}
int model_view_load(V v){return model_view_load_inner(v,NULL);}
int model_view_load_precompiled(V v,const EmScene *prototype){return prototype&&model_view_load_inner(v,prototype);}
int model_view_compile_scene(EmScene *out,V v,size_t limit){
 V src=get(v,"source");char labels[32][128];const char *names[32];unsigned n=0;
 for(unsigned i=0;i<count(src)&&n<32;++i){V item;em_psb_object_at(&item,&src,i);
  if(!get(get(item,"texture"),"pixel").data||!em_psb_object_key(&src,i,labels[n],sizeof labels[n]))continue;
  names[n]=labels[n];++n;
 }
 return n&&em_scene_compile_limited(out,v,names,n,limit);
}
static Mat identity(void){return (Mat){.a=1,.d=1,.alpha=1,.visible=1,.chain=-1,.zx=1,.zy=1,.tint=0x808080ffu,.opacity=255};}
static Mat mul(Mat p,Mat q){Mat r=q;r.a=p.a*q.a+p.c*q.b;r.b=p.b*q.a+p.d*q.b;r.c=p.a*q.c+p.c*q.d;r.d=p.b*q.c+p.d*q.d;r.x=p.a*q.x+p.c*q.y+p.x;r.y=p.b*q.x+p.d*q.y+p.y;r.alpha=p.alpha*q.alpha;r.visible=p.visible&&q.visible;r.z=p.z+q.z;r.chain=p.chain;
 r.angle=p.angle+q.angle;r.zx=p.zx*q.zx;r.zy=p.zy*q.zy;r.shx=p.shx+q.shx;r.shy=p.shy+q.shy;r.fx=p.fx^q.fx;r.fy=p.fy^q.fy;r.clip=p.clip;return r;}
/* 811975A2: flip, shear, scale and rotation in the PSB's explicit order. */
/* Per-view memo of the exact libm input bits:
 * values depend only on angle and the fixed runtime floating-point mode.
 * Matrix arithmetic below remains untouched, including signed-zero behavior. */
/* Hot pose helpers carry the selected view explicitly. This avoids repeated
 * emulated TLS lookup on ARM while keeping ownership private to each model. */
static void shape_trig(EmViewContext *current_view,float angle,float *co,float *si){
 typedef ShapeTrigMemo Entry;Entry *entries=current_view->shape_trig_memo;
 uint32_t bits;memcpy(&bits,&angle,sizeof(bits));
 if((bits&0x7f800000u)==0x7f800000u){*co=cosf(angle);*si=sinf(angle);return;}
 Entry *e=entries+((bits^(bits>>11)^(bits>>22))&7u);
 if(e->valid&&e->bits==bits){*co=e->co;*si=e->si;return;}
 *co=cosf(angle);*si=sinf(angle);*e=(Entry){bits,1,*co,*si};
}
static void shape_calculate(EmViewContext *current_view,Mat *m,const int order[4]){m->a=m->d=1;m->b=m->c=0;for(int i=0;i<4;++i){float a=m->a,b=m->b,c=m->c,d=m->d;
 switch(order[i]){case 0:if(m->fx){m->a=-a;m->c=-c;}if(m->fy){m->b=-b;m->d=-d;}break;
 case 1:{float angle=m->angle*(3.1416f*2.0f)/360.0f,co,si;shape_trig(current_view,angle,&co,&si);m->a=co*a-si*b;m->c=co*c-si*d;m->b=si*a+co*b;m->d=si*c+co*d;}break;
 case 2:m->a*=m->zx;m->c*=m->zx;m->b*=m->zy;m->d*=m->zy;break;
 case 3:m->a=a+m->shx*b;m->c=c+m->shx*d;m->b=m->shy*a+b;m->d=m->shy*c+d;break;
 }}}
/* Exact-input affine memo owned by each view, bounded to 64 entries.
 * Values depend only on these bits and the fixed runtime FP mode. */
static void shape(EmViewContext *current_view,Mat *m,const int order[4]){
 /* Neutral rotation/shear in the standard order is a diagonal matrix.
  * Retain authored scale/flip; only zero signs may differ from the full
  * multiply chain. Nonfinite inputs and other orders keep the original path. */
 if(m->angle==0&&m->shx==0&&m->shy==0&&isfinite(m->zx)&&isfinite(m->zy)&&
    order[0]==0&&order[1]==3&&order[2]==2&&order[3]==1){
  m->a=m->fx?-m->zx:m->zx;m->d=m->fy?-m->zy:m->zy;m->b=m->c=0;return;
 }
 typedef ShapeMemo Entry;Entry *entries=current_view->shape_memo;
 uint32_t key[11];memcpy(key,&m->angle,5*sizeof(float));
 key[5]=(uint32_t)m->fx;key[6]=(uint32_t)m->fy;memcpy(key+7,order,4*sizeof(int));
 for(unsigned i=0;i<5;++i)if((key[i]&0x7f800000u)==0x7f800000u){shape_calculate(current_view,m,order);return;}
 uint32_t hash=2166136261u;for(unsigned i=0;i<11;++i)hash=(hash^key[i])*16777619u;
 Entry *e=entries+((hash^(hash>>16))&63u);
 if(e->valid&&em_exact_bytes(key,e->key,sizeof(key))){m->a=e->result[0];m->b=e->result[1];m->c=e->result[2];m->d=e->result[3];return;}
 shape_calculate(current_view,m,order);memcpy(e->key,key,sizeof(key));
 e->result[0]=m->a;e->result[1]=m->b;e->result[2]=m->c;e->result[3]=m->d;e->valid=1;
}
static void reset_mesh_cache(void){for(unsigned i=0;i<1024;++i)mesh_cache[i].valid=0;deform_count=old_deform_count=0;}
static float parameter(EmViewContext *current_view,EmSceneMotion *motion,int index){
 if(index<0||(unsigned)index>=motion->parameter_count)return 0;
 EmSceneParameter *p=motion->parameters+index;
 if(p->epoch!=scene_epoch){
  float value=model_controls_value_in(current_view->controls_for_pose,p->id);
  if(eye_value>=0&&!strcmp(p->id,"face_eye_open"))value=eye_value;
  if(mouth_value>=0&&(!strcmp(p->id,"face_mouth")||!strcmp(p->id,"face_talk")))value=mouth_value;
  em_parameter_set(NULL,&p->range,0,value);p->frame=p->range.frame;p->epoch=scene_epoch;
 }return p->frame;
}
static float interpolate(float a,float b,float t){return a+(b-a)*t;}
/* PCSG00341 81193E04: adjust the destination across the 0/360 seam,
 * then weighted interpolation and one-turn normalization. 350 -> 0 is
 * a ten-degree turn, not a 350-degree turn through the opposite side. */
static float interpolate_rotation(float from,float to,float t){
 if(from==to)return from;
 if(from>=to){if(from-to>180)to+=360;}
 else if(to-from>180)to-=360;
 float angle=t*to+from*(1-t);
 if(angle<0)angle+=360;else if(angle>=360)angle-=360;
 return angle;
}
static int combined_patch_calculate(EmViewContext *current_view,const EmSceneLayer *l,EmPoint out[16],const float *values){
 /* sample01 1800597D0: clamp variable, (meshCount-1), weighted pair blend.
  * 180058BB0: first 128-byte patch copied, remaining patches added in order.
  * Later entries already contain deltas: subtracting identity again is wrong. */
 int all_neutral=1;
 for(unsigned i=0;i<l->combinator_count;++i){const EmSceneCombinator *q=l->combinators+i;
  float t=((values?values[i]:model_controls_value_in(current_view->controls_for_pose,q->id))-q->begin)/(q->end-q->begin);t=fminf(1,fmaxf(0,t))*(q->count-1);
  unsigned index=(unsigned)t;float f=t-index;const EmPoint *a=q->patches+index*16,*b=index+1<q->count?a+16:a;
  int exact=fabsf(f)<0.00000011920929f;
  all_neutral&=exact&&(int)index==q->neutral;
  em_patch_accumulate((float*)out,(const float*)a,(const float*)b,f,exact,i!=0);
 }
 return !all_neutral;
}
/* Dependency gate for the pure local patch only. Immutable layer data lives
 * until model_view_free; mutable control values are compared bit for bit.
 * Parent composition, anchors, events and physics remain live on every frame. */
static int combined_patch(EmViewContext *current_view,const EmSceneLayer *l,EmPoint out[16]){
 if(!l->combinator_count||l->combinator_count>16)return combined_patch_calculate(current_view,l,out,NULL);
 float values[16];for(unsigned i=0;i<l->combinator_count;++i)values[i]=model_controls_value_in(current_view->controls_for_pose,l->combinators[i].id);
 uintptr_t address=(uintptr_t)l;
 PatchMemo *memo=current_view->patch_memo+(((address>>4)^(address>>10))&63u);
 size_t bytes=l->combinator_count*sizeof(float);
 if(memo->layer==l&&memo->count==l->combinator_count&&em_exact_bytes(memo->values,values,bytes)){
  memcpy(out,memo->points,sizeof memo->points);return memo->active;
 }
 int active=combined_patch_calculate(current_view,l,out,values);
 memo->layer=l;memo->count=l->combinator_count;memcpy(memo->values,values,bytes);
 memcpy(memo->points,out,sizeof memo->points);memo->active=active;return active;
}
/* 811A0CEC: RGB has neutral 128, alpha has neutral 255. */
static uint32_t tint_multiply(uint32_t a,uint32_t b){uint32_t r=((a&255)*(b&255))/255;for(int bit=8;bit<=24;bit+=8){unsigned c=(((a>>bit)&255)*((b>>bit)&255))>>7;r|=(c>255?255:c)<<bit;}return r;}
/* Packed interpolation is unchanged when both authored endpoints agree.
 * Exact zero also returns the original packed word, including alpha. */
static uint32_t tint_interpolate(uint32_t a,uint32_t b,float t){return a==b||t==0?a:em_color_interpolate(a,b,t);}
static void anchor_collect(EmViewContext *current_view,unsigned layer_index,const EmSceneLayer *layer,Mat world){
 if(generating_masks||current_view->anchor_failed||current_view->anchor_instance<0)return;
 /* Unrequested names cannot affect first-match lookup or physics output. */
 AnchorDemandMemo *demand=current_view->anchor_demand+(layer_index&511u);
 if(demand->layer!=layer){demand->needed=model_controls_needs_anchor_in(current_view->controls_for_pose,layer->label);demand->layer=layer;}
 if(!demand->needed)return;
 if(current_view->anchor_candidate_count>=ANCHOR_CANDIDATES){current_view->anchor_failed=1;return;}
 /* 8119F546..59E: point-shape anchors follow the parent deformation chain,
  * before this layer's own new patch is installed. Screen fitting excluded. */
 EmPoint p={world.x,world.y};
 if(layer->type==1&&layer->shape==0)for(int i=world.chain;i>=0;i=deforms[i].next)
  if(deforms[i].inv_det!=0)p=em_mesh_world(&deforms[i].patch,p);
 AnchorCandidate *c=current_view->anchor_candidates+current_view->anchor_candidate_count++;
 *c=(AnchorCandidate){layer->label,p.x,p.y,layer->type==1&&layer->shape==0};
 ++current_view->anchor_instances[current_view->anchor_instance].count;
}
static void anchor_publish(EmViewContext *current_view){
 unsigned n=current_view->anchor_instance_count;if(!n||current_view->anchor_failed){if(current_view->anchor_failed)++missing;return;}
 for(unsigned i=0;i<n;++i)current_view->anchor_nodes[i].count=0;
 for(unsigned i=1;i<n;++i){int parent=current_view->anchor_instances[i].parent;if(parent<0||(unsigned)parent>=i){++missing;return;}++current_view->anchor_nodes[parent].count;}
 unsigned offset=0;
 for(unsigned i=0;i<n;++i){unsigned count=current_view->anchor_nodes[i].count;current_view->anchor_nodes[i].children=current_view->anchor_children+offset;offset+=count;current_view->anchor_nodes[i].count=0;}
 for(unsigned i=1;i<n;++i){EmPhysicsLookupNode *node=current_view->anchor_nodes+current_view->anchor_instances[i].parent;unsigned *children=(unsigned*)node->children;unsigned at=node->count++;
  while(at&&current_view->anchor_instances[children[at-1]].layer>current_view->anchor_instances[i].layer){children[at]=children[at-1];--at;}children[at]=i;
 }
 unsigned count=em_physics_lookup_order(current_view->anchor_nodes,n,0,current_view->anchor_order,ANCHOR_INSTANCES);
 if(!count){++missing;return;}
 for(unsigned i=0;i<count;++i){AnchorInstance *instance=current_view->anchor_instances+current_view->anchor_order[i];
  for(unsigned k=0;k<instance->count;++k){AnchorCandidate *c=current_view->anchor_candidates+instance->first+k;model_controls_anchor_candidate(c->label,c->x,c->y,c->valid);}
 }
}
/* Storage/instance identity are host adapter policy. Producer/crossing rules
 * come from motion_events; event state never aliases repeated references. */
static int event_reserve(EmMotionEventQueue*q,size_t need){
 if(need<=q->capacity)return 1;if(need>1048576)return 0;size_t cap=q->capacity?q->capacity:16;
 while(cap<need)cap*=2;EmMotionEvent*p=realloc(q->records,cap*sizeof(*p));if(!p)return 0;q->records=p;q->capacity=cap;return 1;
}
typedef struct {ViewEventInstance *instance;EmMotionEventQueue *queue;} ViewEventVisit;
static int event_visit(void*opaque,EmMotionClock*clock,int op){
 ViewEventVisit *v=opaque;ViewEventInstance*e=v->instance;EmSceneMotion*m=e->motion;EmMotionEventQueue*q=v->queue;
 if(m->tags){e->tag.frames=m->tags;e->tag.count=m->tag_count;e->tag.tag=1;EmMotionEventPosition p={clock->time,0,0};
  int ok=op==EM_MOTION_SEEK?em_motion_event_seek(&e->tag,&p,q):em_motion_event_step(&e->tag,&p,q,op==EM_MOTION_REVERSE);if(!ok)return 0;
  clock->time=p.time;if(p.aligned||p.synced)clock->absolute=p.time;clock->aligned|=p.aligned;clock->synced|=p.synced;
 }
 for(unsigned j=0;e->layers&&j<m->layer_count;++j){EmSceneLayer*l=m->layers+j;if(!l->event_frames)continue;ViewEventLayer*x=e->layers+j;float t=l->has_parameter?parameter(current_view,m,l->parameter):clock->time;
  x->cursor.frames=l->event_frames;x->cursor.count=l->frame_count;x->cursor.layer=l->label;
  /* 8119ADC0 parameterized traversal updates pose without queuing act. */
  if(op==EM_MOTION_SEEK||!l->has_parameter){EmMotionEventPosition p={t,0,0};int ok=op==EM_MOTION_SEEK?em_motion_event_seek(&x->cursor,&p,q):em_motion_event_step(&x->cursor,&p,q,op==EM_MOTION_REVERSE);if(!ok)return 0;}x->time=t;
 }return 1;
}
static int event_begin(EmSceneMotion*m,const EmSceneContent*source,int layer,int depth,float raw,float *time){
 if(generating_masks){
  /* Re-emitting mask targets reuses this frame's resolved clock (including
   * wrap/align/sync). It must never advance clocks or dispatch events again. */
  for(unsigned i=0;i<current_view->event_instance_count;++i){ViewEventInstance*e=current_view->event_instances+i;
   if(e->parent==current_view->event_instance&&e->layer==layer&&e->motion==m&&e->epoch==scene_epoch){*time=e->time;current_view->event_instance=(int)i;return -1;}}
  current_view->mask_failed_data=1;return -1;
 }
 if(current_view->event_failed)return -1;
 unsigned i;for(i=0;i<current_view->event_instance_count;++i){ViewEventInstance*e=current_view->event_instances+i;if(e->parent==current_view->event_instance&&e->layer==layer&&e->motion==m)break;}
 if(i==current_view->event_instance_count){
  if(i>=ANCHOR_INSTANCES){current_view->event_failed=1;return -1;}
  if(i==current_view->event_instance_capacity){unsigned cap=i?i*2:16;ViewEventInstance*p=realloc(current_view->event_instances,cap*sizeof(*p));if(!p){current_view->event_failed=1;return -1;}current_view->event_instances=p;current_view->event_instance_capacity=cap;}
  int has_layer_events=0;for(unsigned j=0;j<m->layer_count;++j)if(m->layers[j].event_frames){has_layer_events=1;break;}
  ViewEventLayer*layers=has_layer_events?calloc(m->layer_count,sizeof(*layers)):NULL;if(has_layer_events&&!layers){current_view->event_failed=1;return -1;}
  current_view->event_instances[i]=(ViewEventInstance){.parent=current_view->event_instance,.layer=layer,.motion=m,.layers=layers};++current_view->event_instance_count;
 }
 ViewEventInstance*e=current_view->event_instances+i;if(e->epoch==scene_epoch)return -1;int seek=!e->epoch||e->epoch+1!=scene_epoch||e->seek_serial!=current_view->event_seek_serial||e->source_key!=source;
 EmMotionEventQueue*q=current_view->event_queues+depth;q->count=0;
 if(scene.total_event_frames&&!event_reserve(q,10*(size_t)scene.total_event_frames+1)){current_view->event_failed=1;return -1;}
 ViewEventVisit visit={e,q};int ok=1;
 if(m->has_parameter){
  e->clock.absolute=e->clock.time=*time;e->clock.aligned=0;
  if(seek)ok=event_visit(&visit,&e->clock,EM_MOTION_SEEK);
  else if(*time!=e->time)ok=event_visit(&visit,&e->clock,*time<e->time?EM_MOTION_REVERSE:EM_MOTION_FORWARD);
 }else{
  if(seek){
   unsigned synced=e->epoch&&e->source_key==source?e->clock.synced:0;
   e->clock=(EmMotionClock){.absolute=raw,.time=raw,.last=m->length,.loop=m->loop_time,.playing=1,.seek=1,.hold=1,.synced=synced};
   ok=em_motion_clock_step(&e->clock,0,event_visit,&visit);e->clock.hold=0;
  }else ok=em_motion_clock_step(&e->clock,raw-e->raw_time,event_visit,&visit);
 }
 if(!ok){current_view->event_failed=1;return -1;}*time=e->clock.time;
 if(depth==0){current_view->motion_clock=e->clock.absolute;raw=e->clock.absolute;}
 e->time=*time;e->raw_time=raw;e->epoch=scene_epoch;e->seek_serial=current_view->event_seek_serial;e->source_key=source;current_view->event_instance=(int)i;return (int)i;
}
static void event_finish(int instance,int depth,int previous){
 if(instance>=0&&scene.total_event_frames&&!current_view->event_failed&&depth){EmMotionEventQueue*p=current_view->event_queues+depth-1,*c=current_view->event_queues+depth;
  if(!event_reserve(p,p->count+c->count)||!em_motion_event_prepend(p,c))current_view->event_failed=1;
 }current_view->event_instance=previous;
}
static void event_publish(void){
 if(!scene.total_event_frames||current_view->event_failed)return;
 EmMotionEventQueue*p=&current_view->event_output,*q=current_view->event_queues;
 if(!event_reserve(p,p->count+q->count)){current_view->event_failed=1;return;}
 for(size_t i=0;i<q->count;++i)p->records[p->count++]=q->records[i];q->count=0;
}
/* Per-view diagnostics, sampled every120 draws; owner reads after join. Times
 * include instrumentation overhead; combined is a subset of patch time. */
#define pose_parts (current_view->pose_parts_data)
#define pose_parts_active (current_view->pose_parts_active_data)
static uint64_t pose_mark(EmViewContext *current_view){return pose_parts_active?sceKernelGetSystemTimeWide():0;}
static void pose_add(EmViewContext *current_view,unsigned slot,uint64_t mark){if(pose_parts_active)pose_parts[slot]+=sceKernelGetSystemTimeWide()-mark;}
void model_view_take_pose_parts(uint64_t out[10]){if(out){memcpy(out,pose_parts,sizeof pose_parts);memset(pose_parts,0,sizeof pose_parts);}}
static void local_pose_calculate(EmViewContext *current_view,const EmSceneLayer *l,const EmSceneFrame *f,const EmSceneFrame *next,float blend,LocalPoseValue *value){
  const EmSceneContent *c=&f->content,*nc=&next->content;Mat m=identity();m.visible=f->type!=0;
  float *ratios=value->ratios;memset(ratios,0,5*sizeof(float));if(blend>0)for(int k=0;k<5;++k){
   /* Identical authored curve and blend have identical pure sample results. */
   int previous=0;while(previous<k&&c->curves[previous]!=c->curves[k])++previous;
   ratios[k]=previous<k?ratios[previous]:em_scene_curve(&scene,c->curves[k],blend);
  }
  m.x=interpolate(c->coord[0],nc->coord[0],ratios[0]);m.y=interpolate(c->coord[1],nc->coord[1],ratios[0]);m.z=interpolate(c->coord[2],nc->coord[2],ratios[0]);
  if(blend>0&&c->path&&(l->coordinate==0||l->coordinate==1)){
   const EmPath *path=c->path;int32_t segment=0,node=0;
   float ratio=fminf(path->times_end[-1],fmaxf(path->times[0],ratios[0]));
   EmPathPoint p=em_path_sample(path,&segment,&node,ratio);
   int axis=l->coordinate==0?1:2;float dx=nc->coord[0]-c->coord[0],dy=nc->coord[axis]-c->coord[axis];
   m.x=(c->coord[0]+dx*p.x)-dy*p.y;
   float second=(c->coord[axis]+dy*p.x)+dx*p.y;
   if(axis==1)m.y=second;else m.z=second;
  }
  m.zx=interpolate(c->zx,nc->zx,ratios[3]);m.zy=interpolate(c->zy,nc->zy,ratios[3]);m.fx=c->fx;m.fy=c->fy;
  /* 81193E04 copies the authored angle for HOLD/zero/sub-epsilon raw blend.
   * A tween whose easing returns zero still performs one-turn normalization. */
  m.angle=blend>=0x1p-23f?interpolate_rotation(c->angle,nc->angle,ratios[2]):c->angle;m.shx=interpolate(c->shear_x,nc->shear_x,ratios[4]);m.shy=interpolate(c->shear_y,nc->shear_y,ratios[4]);
 value->opacity=(unsigned)roundf(blend*nc->opa+c->opa*(1-blend));m.alpha=value->opacity/255.f;
 for(int k=0;k<4;++k)value->colors[k]=k&&c->colors[k]==c->colors[0]&&nc->colors[k]==nc->colors[0]?value->colors[0]:tint_interpolate(c->colors[k],nc->colors[k],ratios[1]);
 value->pose=m;
}
/* Empty frames also have immutable local inputs. Cache their invisible local
 * pose; frame/next/seek changes still invalidate it, and world/child work stays live. */
static const LocalPoseValue *local_pose(EmViewContext *current_view,unsigned index,const EmSceneLayer *l,const EmSceneFrame *f,const EmSceneFrame *next,float blend,LocalPoseValue *fallback){
 if(index>=current_view->local_pose_count||!isfinite(blend)){local_pose_calculate(current_view,l,f,next,blend,fallback);return fallback;}
 LocalPoseMemo *memo=current_view->local_poses+index;
 int force=memo->frame!=f||memo->next!=next||memo->seek!=current_view->event_seek_serial;
 if(em_pose_update_gate(&memo->ratio,force,0,0,f->type==3&&next->type!=0,blend)){
  local_pose_calculate(current_view,l,f,next,blend,&memo->value);memo->frame=f;memo->next=next;memo->seek=current_view->event_seek_serial;
 }
 return &memo->value;
}
/* Only the direct parent's raw local patch participates in this kernel.
 * World affine/ancestor patches are deliberately applied after this gate. */
static void parent_sync(EmViewContext *current_view,const EmSceneLayer *l,EmMeshChildPose *pose,const EmPoint patch[16],int w,int h,EmPoint offset,unsigned sync,unsigned inherit){
 /* The kernel always consumes position, but only consumes angle/scale
  * when both the parent sync bit and the child inheritance bit enable it.
  * Keep ignored components live on a hit rather than replaying stale values. */
 int angle=(sync&2)&&(inherit&0x10),sx=(sync&4)&&(inherit&0x20),sy=(sync&4)&&(inherit&0x40);
 EmMeshChildPose input=*pose;
 if(!angle)input.angle=0;if(!sx)input.scale_x=0;if(!sy)input.scale_y=0;
 uintptr_t address=(uintptr_t)l;ParentSyncMemo *memo=current_view->parent_sync+(((address>>4)^(address>>12))&127u);
 if(memo->layer==l&&memo->w==w&&memo->h==h&&memo->sync==sync&&memo->inherit==inherit&&
    !memcmp(&memo->offset,&offset,sizeof offset)&&!memcmp(&memo->input,&input,sizeof input)&&em_exact_bytes(memo->patch,patch,sizeof memo->patch)){
  pose->position=memo->output.position;if(angle)pose->angle=memo->output.angle;
  if(sx)pose->scale_x=memo->output.scale_x;if(sy)pose->scale_y=memo->output.scale_y;return;
 }
 memo->layer=l;memo->w=w;memo->h=h;memo->sync=sync;memo->inherit=inherit;memo->offset=offset;memo->input=input;memcpy(memo->patch,patch,sizeof memo->patch);
 em_mesh_child_sync(pose,patch,w,h,offset,sync,inherit);memo->output=*pose;
}
static void flatten(EmViewContext *current_view,Node *nodes,Mat parent,EmSceneMotion *motion,float time){
 for(unsigned i=0;i<motion->layer_count;++i){uint64_t part=pose_mark(current_view);if(pose_parts_active)++pose_parts[9];const EmSceneLayer *l=motion->layers+i;
  Mat inherited=l->parent<0?parent:nodes[l->parent].m;
  if(l->parent>=0){inherited.chain=nodes[l->parent].child_chain;inherited.clip=nodes[l->parent].child_clip;}
  float t=l->has_parameter?parameter(current_view,motion,l->parameter):time;
  const EmSceneFrame *f=&scene.empty_frame,*next=&scene.empty_frame;unsigned fi=em_scene_frame_index(l,t);
  if(l->frame_count)f=l->frames+fi;
  if(fi+1<l->frame_count)next=l->frames+fi+1;
  float blend=0,delta=next->time-f->time;
  if(f->type==3&&next->type!=0&&delta>0){float elapsed=t-f->time;
   if(f->content.time_interval>0)elapsed=truncf(elapsed/f->content.time_interval)*f->content.time_interval;
   blend=fminf(1,fmaxf(0,elapsed/delta));}
  const EmSceneContent *c=&f->content,*nc=&next->content;LocalPoseValue fallback;
  const LocalPoseValue *local=local_pose(current_view,motion->pose_offset+i,l,f,next,blend,&fallback);Mat m=local->pose;
  pose_add(current_view,1,part);part=pose_mark(current_view);
  /* 811A0712, before parent affine: mask bit1 samples only the direct
   * parent's original normalized patch. bit8's world chain is independent.
   * Parent content ox/oy is additive to icon origin, not sprite override. */
  if(l->parent>=0){Node *pn=nodes+l->parent;const EmSceneLayer *pl=pn->layer;
   if((pl->mesh_sync_child_mask&1)&&pl->mesh_transform==1&&pn->m.visible&&pn->own_deform>=0){
    const Deform *pd=deforms+pn->own_deform;const EmSceneContent *pc=pn->content;
    EmMeshChildPose q={{m.x,pl->coordinate?m.z:m.y},m.angle,m.zx,m.zy};
    parent_sync(current_view,l,&q,pd->raw_cp,(int)pd->w,(int)pd->h,
                       (EmPoint){pc->origin_x+pc->content_origin_x,pc->origin_y+pc->content_origin_y},
                       pl->mesh_sync_child_mask,l->inherit_mask);
    m.x=q.position.x;if(pl->coordinate)m.z=q.position.y;else m.y=q.position.y;
    m.angle=q.angle;m.zx=q.scale_x;m.zy=q.scale_y;
   }
  }
  pose_add(current_view,2,part);part=pose_mark(current_view);
  /* 8119441C..81194448: standalone opa remains linear. occ applies to
   * packed vertex colors (including their alpha), not this scalar. */
  m.alpha=local->pose.alpha;
  /* Component inheritance rebuilds world a/b/c/d below. Its local matrix
   * never contributes to the resulting pose; translation uses parent only. */
  if((l->inherit_mask&0x1fc)==0x1fc)shape(current_view,&m,l->transform_order);
  Mat world=mul(inherited,m);
  /* 811A03C2 uses component inheritance when not all affine bits are set.
   * Translation always follows the parent's matrix, independently of these. */
  if((l->inherit_mask&0x1fc)!=0x1fc){
   world.fx=m.fx^((l->inherit_mask&4)?inherited.fx:0);world.fy=m.fy^((l->inherit_mask&8)?inherited.fy:0);
   world.angle=m.angle+((l->inherit_mask&16)?inherited.angle:0);
   world.zx=m.zx*((l->inherit_mask&32)?inherited.zx:1);world.zy=m.zy*((l->inherit_mask&64)?inherited.zy:1);
   world.shx=m.shx+((l->inherit_mask&128)?inherited.shx:0);world.shy=m.shy+((l->inherit_mask&256)?inherited.shy:0);shape(current_view,&world,l->transform_order);
  }
  if(!(l->inherit_mask&0x400))world.alpha=m.alpha;
  if(current_view->material_profile==1){
   /* Original dummy-root opacity is the reference's effective byte. Each
    * layer truncates separately; no float alpha survives into color packing. */
   unsigned local_opacity=local->opacity;
   unsigned inherited_opa=(l->inherit_mask&0x400)?inherited.opacity:parent.independent_inherit?255:parent.opacity;
   world.opacity=local_opacity*inherited_opa/255;world.alpha=world.opacity/255.f;
   world.tint=parent.tint;world.independent_inherit=parent.independent_inherit;
  }
  anchor_collect(current_view,motion->pose_offset+i,l,world);
  unsigned child_clip=world.clip;
  /* 8119F0A8: a packet uses its nearest ACTIVE ancestor; its own mask applies
   * to descendants, never to itself. The current-frame +16 sentinel is type0.
   * Drawable availability here is the adapter's decoded sprite/blank content. */
  int drawable=l->type==0?(c->atlas>=0&&c->icon_valid&&!c->skip_sprite):(c->width>0&&c->height>0);
  if(!generating_masks&&!current_view->anchor_only&&l->stencil_type&&world.visible&&f->type!=0&&
     ((l->type!=0&&l->type!=11&&l->type!=12)||drawable)){
   if(view_mask_count<128){MaskGroup *g=mask_groups+view_mask_count;*g=(MaskGroup){.layer=l,.ancestor=world.clip};child_clip=++view_mask_count;}
   else current_view->mask_failed_data=1;
  }
  if(!(l->inherit_mask&0x2000000))world.chain=-1;
  pose_add(current_view,3,part);part=pose_mark(current_view);
  int own_deform=-1,child_chain=world.chain;
  if(l->mesh_transform==1&&(c->has_patch||nc->has_patch||l->combinator_count)&&deform_count<1024){
   Deform d={.m=world,.w=c->width,.h=c->height,.ox=c->origin_x,.oy=c->origin_y,.next=world.chain};int active=0;
   if(l->combinator_count){uint64_t combined=pose_mark(current_view);active=combined_patch(current_view,l,d.cp);pose_add(current_view,8,combined);}
   else{
   for(int k=0;k<16;++k){float x=(k%4)/3.0f,y=(k/4)/3.0f;
    d.cp[k]=(EmPoint){interpolate(c->cp[k].x,nc->cp[k].x,blend),interpolate(c->cp[k].y,nc->cp[k].y,blend)};
    if(fabsf(d.cp[k].x-x)>1e-7f||fabsf(d.cp[k].y-y)>1e-7f)active=1;
   }
   }
   if(active&&d.w>0&&d.h>0){
    memcpy(d.raw_cp,d.cp,sizeof(d.cp));
    /* 8119F5F6..8119F674 traverses direct parents, adding their authored
     * normalized patches before the CHILD world transform. It does not test
     * equal rectangles/matrices. Keep raw patches to avoid adding a parent's
     * already combined ancestor twice. Bit 8 enables child mesh propagation. */
    if(l->mesh_combine&&(l->mesh_sync_child_mask&8))for(int j=l->parent;j>=0;j=motion->layers[j].parent){
     int index=nodes[j].own_deform;
     if(index>=0&&(motion->layers[j].mesh_sync_child_mask&8)){Deform *p=deforms+index;
      em_patch_combine((float*)d.cp,(const float*)p->raw_cp);
      if(d.next==index)d.next=p->next;
     }
     if(!motion->layers[j].mesh_combine)break;
    }
    /* Frame-constant inverse factors; retain the recovered patch evaluation.
     * Avoid four scalar divisions at every vertex of every deformation. */
    float determinant=d.m.a*d.m.d-d.m.b*d.m.c;
    d.inv_det=fabsf(determinant)<1e-10f?0:1/determinant;d.inv_w=1/d.w;d.inv_h=1/d.h;
    /* 8119F676 onward: scale the forward matrix by the mesh rectangle,
     * map all control points once, retain its inverse for world sampling. */
    float a=d.w*d.m.a,b=d.h*d.m.c,c0=d.w*d.m.b,d0=d.h*d.m.d;
    float tx=(-d.m.a*d.ox-d.m.c*d.oy)+d.m.x,ty=(-d.m.b*d.ox-d.m.d*d.oy)+d.m.y;
    float det=a*d0-b*c0;
    if(d.inv_det!=0){d.patch.inverse[0]=d0/det;d.patch.inverse[1]=-b/det;d.patch.inverse[2]=-c0/det;d.patch.inverse[3]=a/det;}
    d.patch.offset[0]=-tx;d.patch.offset[1]=-ty;
    em_patch_transform((float*)d.patch.control,(const float*)d.cp,a,b,c0,d0,tx,ty);
    d.grid_bounded=d.inv_det!=0&&em_mesh_world_grid_bounded(&d.patch);
    world.chain=own_deform=deform_count;
    if(l->mesh_sync_child_mask&8)child_chain=deform_count;
    deform_changed[deform_count]=deform_count>=old_deform_count||deforms[deform_count].next!=d.next||(deforms[deform_count].inv_det==0)!=(d.inv_det==0)||!em_exact_bytes(&deforms[deform_count].patch,&d.patch,sizeof(d.patch));
    deforms[deform_count++]=d;++nonneutral;
   }
  }
  pose_add(current_view,4,part);part=pose_mark(current_view);
  uint32_t parent_tint=(l->inherit_mask&0x200)?inherited.tint:0x808080ffu;
  uint32_t colors[4];
  for(int k=0;k<4;++k)colors[k]=k&&local->colors[k]==local->colors[0]?colors[0]:
   current_view->material_profile==1?local->colors[k]:tint_multiply(local->colors[k],parent_tint);
  if(current_view->material_profile!=1)world.tint=colors[0];
  /* 8119DF08: referenced-motion clock = layer clock - key time + offset. */
  nodes[i]=(Node){.layer=l,.content=c,.m=world,.type=l->type,.time=fmaxf(0,t-f->time+c->child_time_offset),.own_deform=own_deform,.child_chain=child_chain,.active=f->type!=0,.layer_index=(int)i,.child_clip=child_clip};
  memcpy(nodes[i].colors,colors,sizeof colors);
  pose_add(current_view,5,part);
 }
}
static void motion_scene(EmViewContext *current_view,EmSceneMotion *motion,Mat parent,float time,int depth,int reference_layer,const EmSceneContent *source_key);
static void emit(EmViewContext *current_view,Node *n,int depth){
 const EmSceneContent *c=n->content;
 if(n->type==3){n->reference_emitted=1;if(!n->active)return;Mat child=n->m;child.chain=n->child_chain;child.clip=n->child_clip;
  if(current_view->material_profile==1){child.tint=tint_multiply(n->colors[0],(n->layer->inherit_mask&0x200)?n->m.tint:0x808080ffu);child.independent_inherit=n->layer->motion_independent_layer_inherit;}
  if(c->child_motion>=0){int previous=current_view->anchor_only;current_view->anchor_only|=!n->m.visible||n->m.alpha<=0;motion_scene(current_view,scene.motions+c->child_motion,child,n->time,depth+1,n->layer_index,c);current_view->anchor_only=previous;}
  else if(n->m.visible&&n->m.alpha>0)++missing;return;
 }
 if(current_view->anchor_only||!n->m.visible||n->m.alpha<=0||(n->type!=0&&n->type!=12)||c->skip_sprite)return;
 if(c->atlas<0||!c->icon_valid){++missing;return;}if(sprite_count>=1024){current_view->mask_failed_data=1;return;}
 Sprite *s=sprites+sprite_count++;*s=(Sprite){.t=textures+c->atlas,.m=n->m,.left=c->left,.top=c->top,.w=c->w,.h=c->h,.attr=c->attr,.ox=c->ox,.oy=c->oy,.stencil_write=mask_write,.blend_mode=c->blend_mode,.original_material=current_view->material_profile==1};memcpy(s->colors,n->colors,sizeof(s->colors));
 /* Hard-stencil writes retain the adapter shader/alpha policy; they are not
  * claimed as original soft-mask material recovery. Ordinary profile1 colors
  * apply effective opacity and motion tint BEFORE integer grid interpolation. */
 if(current_view->material_profile==1)for(int k=0;k<4;++k)s->colors[k]=em_material_prepare_color(n->colors[k],s->original_material?(uint8_t)n->m.opacity:255,n->m.tint);
}
static void motion_scene(EmViewContext *current_view,EmSceneMotion *motion,Mat parent,float time,int depth,int reference_layer,const EmSceneContent *source_key){
 if(depth>8){++missing;return;}
 int previous_event=current_view->event_instance;float raw_time=time;
 int previous_instance=current_view->anchor_instance;
 if(!generating_masks&&!current_view->anchor_failed){
  if(current_view->anchor_instance_count>=ANCHOR_INSTANCES)current_view->anchor_failed=1;
  else{unsigned index=current_view->anchor_instance_count++;current_view->anchor_instances[index]=(AnchorInstance){previous_instance,reference_layer,current_view->anchor_candidate_count,0};current_view->anchor_instance=(int)index;}
 }
 if(motion->has_parameter)time=parameter(current_view,motion,motion->parameter);
 uint64_t event_mark=pose_mark(current_view);int event_instance=event_begin(motion,source_key,reference_layer,depth,raw_time,&time);pose_add(current_view,6,event_mark);
 if(generating_masks&&current_view->mask_failed_data){current_view->event_instance=previous_event;return;}
 unsigned first_mask=view_mask_count;Node *nodes=node_stack[depth];flatten(current_view,nodes,parent,motion,time);
 if(!generating_masks&&!current_view->anchor_only)for(unsigned g=first_mask;g<view_mask_count;++g){MaskGroup *group=mask_groups+g;const EmSceneLayer *l=group->layer;
  group->depth=depth;group->owner_event=event_instance;
  if(l->stencil_type&4){unsigned index=(unsigned)(l-motion->layers);if(l->type==0||l->type==12)group->targets[group->count++]=nodes[index];for(unsigned k=0;k<l->mask_count;++k){group->targets[group->count]=nodes[l->mask_layers[k]];group->targets[group->count++].m.clip=0;}}
  else {unsigned index=(unsigned)(l-motion->layers);group->targets[group->count++]=nodes[index];group->targets[0].m.clip=0;}
 }
 const EmScenePriority *order=NULL;
 for(unsigned i=0;i<motion->priority_count;++i){if(i&&motion->priorities[i].time>time)break;order=motion->priorities+i;}
 if(order&&order->count){for(int i=(int)order->count-1;i>=0;--i){int j=order->indices[i];if(j>=0&&(unsigned)j<motion->layer_count)emit(current_view,nodes+j,depth);}}
 else for(unsigned i=0;i<motion->layer_count;++i)emit(current_view,nodes+i,depth);
 /* Lookup includes valid references omitted by draw priority as well. */
 if(!generating_masks)for(unsigned i=0;i<motion->layer_count;++i)if(nodes[i].type==3&&!nodes[i].reference_emitted){int previous=current_view->anchor_only;current_view->anchor_only=1;emit(current_view,nodes+i,depth);current_view->anchor_only=previous;}
 current_view->anchor_instance=previous_instance;event_mark=pose_mark(current_view);event_finish(event_instance,depth,previous_event);pose_add(current_view,6,event_mark);
}
static void corner(EmViewContext *current_view,Sprite *s,float x,float y,float *ox,float *oy){
 x-=s->ox;y-=s->oy;float px=s->m.a*x+s->m.c*y+s->m.x,py=s->m.b*x+s->m.d*y+s->m.y;
 for(int i=s->m.chain;i>=0;i=deforms[i].next){Deform *d=deforms+i;if(d->inv_det==0)continue;
  EmPoint q=em_mesh_world(&d->patch,(EmPoint){px,py});px=q.x;py=q.y;
 }*ox=px;*oy=py;
}
static void sprite_points(EmViewContext *current_view,Sprite *s,int steps,EmPoint *points){
 /* Retain the original multiply/add order; only identical row/column terms
  * are reused. No reciprocal approximation or reassociation. */
 float ax[9],bx[9];
 for(int x=0;x<=steps;++x){float px=s->w*(float)x/steps-s->ox;ax[x]=s->m.a*px;bx[x]=s->m.b*px;}
 for(int y=0;y<=steps;++y){float py=s->h*(float)y/steps-s->oy,cy=s->m.c*py,dy=s->m.d*py;
  for(int x=0;x<=steps;++x)points[y*(steps+1)+x]=(EmPoint){ax[x]+cy+s->m.x,bx[x]+dy+s->m.y};}
 for(int i=s->m.chain;i>=0;i=deforms[i].next)if(deforms[i].inv_det!=0)em_mesh_world_grid_prepared(&deforms[i].patch,points,(steps+1)*(steps+1),deforms[i].grid_bounded);
}
/* Hoist UVs, channel unpacking and bilinear column work out of each
 * vertex. Ratios and arithmetic order match the previous adapter exactly. */
/* Each category has an independent exact-input key. Deformation animation
 * updates positions without redoing unchanged UV/color work. */
static void pack_vertices(EmViewContext *current_view,const Sprite *s,const EmPoint *points,int steps,GpuVertex *grid,int width,int height,int position,int color,int uv){
 int n=(steps+1)*(steps+1);
 if(position)for(int i=0;i<n;++i){GpuVertex *v=grid+i;
  v->x=(points[i].x*fit_scale+fit_x)*2/width-1;v->y=1-(points[i].y*fit_scale+fit_y)*2/height;v->z=.5f;v->w=1;
 }
 if(uv){float us[9],vs[9];
  for(int x=0;x<=steps;++x)us[x]=(s->left+s->w*(float)x/steps)/s->t->w;
  for(int y=0;y<=steps;++y)vs[y]=(s->top+s->h*(float)y/steps)/s->t->h;
  for(int y=0;y<=steps;++y)for(int x=0;x<=steps;++x){GpuVertex *v=grid+y*(steps+1)+x;v->u=us[x];v->v=vs[y];}
 }
 if(!color)return;
 if(s->original_material){
  uint32_t corners[4]={s->colors[0],s->colors[1],s->colors[3],s->colors[2]},packed[81];
  if(!em_material_color_grid(corners,steps,steps,packed,81))return;
  for(int i=0;i<n;++i){GpuVertex *v=grid+i;uint32_t c=packed[i];
   v->r=(c>>24)/255.f;v->g=((c>>16)&255)/255.f;v->b=((c>>8)&255)/255.f;v->a=(c&255)/255.f;
  }return;
 }
 float top[9][4],bottom[9][4],constant[4];
 int uniform=s->colors[0]==s->colors[1]&&s->colors[0]==s->colors[2]&&s->colors[0]==s->colors[3];
 for(int k=0;k<4;++k)constant[k]=((s->colors[0]>>(24-k*8))&255)/(k==3?255.f:128.f);
 if(!uniform)for(int x=0;x<=steps;++x){float tx=(float)x/steps;
  for(int k=0;k<4;++k){int bit=24-k*8;top[x][k]=interpolate((s->colors[0]>>bit)&255,(s->colors[1]>>bit)&255,tx);bottom[x][k]=interpolate((s->colors[2]>>bit)&255,(s->colors[3]>>bit)&255,tx);}
 }
 for(int y=0;y<=steps;++y){float ty=(float)y/steps;
  for(int x=0;x<=steps;++x){GpuVertex *v=grid+y*(steps+1)+x;float channels[4];
   if(uniform)memcpy(channels,constant,sizeof(channels));else for(int k=0;k<4;++k)channels[k]=interpolate(top[x][k],bottom[x][k],ty)/(k==3?255.f:128.f);
   v->r=channels[0];v->g=channels[1];v->b=channels[2];v->a=s->m.alpha*channels[3];
  }
 }
}
static int same_float(float a,float b){return memcmp(&a,&b,sizeof(a))==0;}
static int same_geometry(const Sprite *a,const Sprite *b){
 return same_float(a->w,b->w)&&same_float(a->h,b->h)&&same_float(a->ox,b->ox)&&same_float(a->oy,b->oy)&&
  same_float(a->m.a,b->m.a)&&same_float(a->m.b,b->m.b)&&same_float(a->m.c,b->m.c)&&same_float(a->m.d,b->m.d)&&
  same_float(a->m.x,b->m.x)&&same_float(a->m.y,b->m.y)&&a->m.chain==b->m.chain;
}
/* Jobs only read the frozen current view and write their disjoint cache range.
 * Evaluation has finished before this pool starts. GXM stays on the owner.
 * Main waits for both completions before submitting or switching/freeing views. */
typedef struct {
 EmViewContext *view;
 int first,last,width,height,profile;unsigned reused,rebuilt;
 unsigned changes[6]; /* geometry, base inputs, deformation only, UV, color, reset */
 SceInt64 geometry_us,vertices_us,wall_us;
#if EM_VERTEX_WORKERS
#if defined(EM_VERTEX_THREADS_HOST)
 fenv_t fp;
#else
 uint32_t fpscr;
#endif
#endif
} VertexJob;
static void vertex_update_range(VertexJob *job){
 EmViewContext *previous=model_view_context_use(job->view?job->view:current_view);
 int width=job->width,height=job->height,profile=job->profile;
 unsigned reused=0,rebuilt=0;
 SceInt64 geometry_us=0,vertices_us=0,mark=0,range_start=profile?sceKernelGetSystemTimeWide():0;
 for(int i=job->first;i<job->last;++i){Sprite *s=sprites+i;int steps=sprite_grid_steps(s);MeshCache *cache=mesh_cache+i;GpuVertex *grid=cache->grid;
  const Sprite *old=&cache->key;int reset=!cache->valid||cache->epoch!=scene_epoch-1||(old->m.chain<0)!=(s->m.chain<0);
  int dirty=reset||!same_geometry(old,s)||!same_float(cache->scale,fit_scale)||!same_float(cache->x,fit_x)||!same_float(cache->y,fit_y)||cache->width!=width||cache->height!=height;
  int uv=reset||old->t!=s->t||!same_float(old->left,s->left)||!same_float(old->top,s->top)||!same_float(old->w,s->w)||!same_float(old->h,s->h);
  int color=reset||old->original_material!=s->original_material||memcmp(old->colors,s->colors,sizeof(s->colors))||(!s->original_material&&!same_float(old->m.alpha,s->m.alpha));
  int base_dirty=dirty;
  if(!dirty)for(int j=s->m.chain;j>=0;j=deforms[j].next)if(deform_changed[j]){dirty=1;break;}
  job->changes[0]+=!!dirty;job->changes[1]+=!!base_dirty;job->changes[2]+=dirty&&!base_dirty;
  job->changes[3]+=!!uv;job->changes[4]+=!!color;job->changes[5]+=!!reset;
  if(dirty||uv||color){++rebuilt;
  if(profile)mark=sceKernelGetSystemTimeWide();
  EmPoint points[81];if(dirty)sprite_points(current_view,s,steps,points);
  if(profile){SceInt64 now=sceKernelGetSystemTimeWide();geometry_us+=now-mark;mark=now;}
  pack_vertices(current_view,s,points,steps,grid,width,height,dirty,color,uv);
   cache->key=*s;cache->scale=fit_scale;cache->x=fit_x;cache->y=fit_y;cache->width=width;cache->height=height;cache->valid=1;
   if(profile)vertices_us+=sceKernelGetSystemTimeWide()-mark;
  }else ++reused;
  cache->epoch=scene_epoch;

 }
 current_view=previous;
 job->reused=reused;job->rebuilt=rebuilt;
 job->geometry_us=geometry_us;job->vertices_us=vertices_us;if(profile)job->wall_us=sceKernelGetSystemTimeWide()-range_start;
}
unsigned model_view_partition_vertices(unsigned cuts[EM_VIEW_VERTEX_PARTS+1]){
 cuts[0]=0;if(!current_view->evaluation_ready||sprite_count<=0)return 0;
 unsigned weight[1024],total=0,parts=(unsigned)sprite_count<EM_VIEW_VERTEX_PARTS?(unsigned)sprite_count:EM_VIEW_VERTEX_PARTS;
 for(int i=0;i<sprite_count;++i){unsigned depth=0;for(int j=sprites[i].m.chain;j>=0;j=deforms[j].next)++depth;
  weight[i]=sprite_grid_vertices(sprites+i)*(depth+1);total+=weight[i];}
 unsigned sum=0,part=1;
 for(unsigned i=0;i<(unsigned)sprite_count&&part<parts;++i){sum+=weight[i];
  if(sum>=total*part/parts||(unsigned)sprite_count-i-1==parts-part)cuts[part++]=i+1;}
 cuts[part]=(unsigned)sprite_count;return part;
}
void model_view_run_vertices(unsigned first,unsigned last,int width,int height,EmViewVertexStats *out){
 memset(out,0,sizeof(*out));if(!current_view->evaluation_ready||first>last||last>(unsigned)sprite_count)return;
 VertexJob job={.view=current_view,.first=(int)first,.last=(int)last,.width=width,.height=height};
 job.profile=scene_epoch%121==0;
 vertex_update_range(&job);out->reused=job.reused;out->rebuilt=job.rebuilt;memcpy(out->changes,job.changes,sizeof(out->changes));
 out->geometry_us=job.geometry_us;out->vertices_us=job.vertices_us;
}
void model_view_accept_vertices(int width,int height,const EmViewVertexStats *stats){
 if(!current_view->evaluation_ready)return;
 current_view->prepared_vertices=*stats;current_view->vertices_width=width;current_view->vertices_height=height;current_view->vertices_ready=1;
}
#if EM_VERTEX_WORKERS
typedef struct {
 VertexJob job;int stop,started;
#if defined(EM_VERTEX_THREADS_HOST)
 pthread_t thread;sem_t start,done;int start_ready,done_ready;
#else
 pthread_t thread;SceUID start,done;int priority,affinity,setup_result;
#endif
} VertexWorker;
static VertexWorker vertex_workers[2];static int vertex_pool_ready,vertex_pool_attempted;
#if defined(EM_VERTEX_THREADS_HOST)
static void vertex_sem_wait(sem_t *s){int r;do{r=sem_wait(s);}while(r&&errno==EINTR);if(r)abort();__sync_synchronize();}
static void vertex_sem_signal(sem_t*s){__sync_synchronize();if(sem_post(s))abort();}
static void *vertex_worker_main(void *arg){VertexWorker *w=arg;
 for(;;){vertex_sem_wait(&w->start);if(w->stop)break;fesetenv(&w->job.fp);vertex_update_range(&w->job);vertex_sem_signal(&w->done);}return NULL;
}
#else
static void vertex_sync_error(const char *op,int rc){FILE*f=fopen("ux0:data/emote-workers.txt","a");if(f){fprintf(f,"workers_sync_failed op=%s rc=%d; abort before unsafe reuse\n",op,rc);fclose(f);}abort();}
/* CPU publication/acquisition barriers, not GPU fences. A failed wait must
 * never turn into early cache reuse while another CPU may still be writing. */
static void vertex_vita_wait(SceUID sem){int rc=sceKernelWaitSema(sem,1,NULL);if(rc<0)vertex_sync_error("wait",rc);__sync_synchronize();}
static void vertex_vita_signal(SceUID sem){__sync_synchronize();int rc=sceKernelSignalSema(sem,1);if(rc<0)vertex_sync_error("signal",rc);}
static void *vertex_worker_main(void *arg){VertexWorker *w=arg;
 SceUID self=sceKernelGetThreadId();w->setup_result=sceKernelChangeThreadPriority(self,w->priority);
 if(w->setup_result>=0)w->setup_result=sceKernelChangeThreadCpuAffinityMask(self,w->affinity);
 vertex_vita_signal(w->done); /* Publish setup before accepting any work. */
 for(;;){vertex_vita_wait(w->start);if(w->stop)break;
  __asm__ volatile("vmsr fpscr, %0"::"r"(w->job.fpscr):"memory");
  vertex_update_range(&w->job);vertex_vita_signal(w->done);
 }return NULL; /* pthread runs TLS destructors; join retires newlib state. */
}
#endif
static void vertex_pool_stop(void){
 int joined=0,deleted=0,sem_deleted=0;
 for(int i=0;i<2;++i){VertexWorker *w=vertex_workers+i;if(!w->started)continue;w->stop=1;
#if defined(EM_VERTEX_THREADS_HOST)
  vertex_sem_signal(&w->start);if(pthread_join(w->thread,NULL))abort();++joined;
#else
  vertex_vita_signal(w->start);int rc=pthread_join(w->thread,NULL);if(rc){FILE*f=fopen("ux0:data/emote-workers.txt","a");if(f){fprintf(f,"workers_join_failed index=%d rc=%d\n",i,rc);fclose(f);}abort();}++joined;++deleted;
#endif
  w->started=0;
 }
 if(vertex_pool_attempted)for(int i=0;i<2;++i){VertexWorker *w=vertex_workers+i;
#if defined(EM_VERTEX_THREADS_HOST)
  if(w->done_ready&&sem_destroy(&w->done)==0)++sem_deleted;if(w->start_ready&&sem_destroy(&w->start)==0)++sem_deleted;if(w->start_ready&&w->done_ready)++deleted;
#else
  if(w->done>=0&&sceKernelDeleteSema(w->done)>=0)++sem_deleted;if(w->start>=0&&sceKernelDeleteSema(w->start)>=0)++sem_deleted;
#endif
 }
#if !EM_LOG_DISABLED
 if(vertex_pool_attempted){FILE *f=fopen("ux0:data/emote-workers.txt","a");if(f){fprintf(f,"workers_stop joined=%d deleted=%d sem_deleted=%d before_model_release=1\n",joined,deleted,sem_deleted);fclose(f);}}
#endif
 memset(vertex_workers,0,sizeof(vertex_workers));
#if !defined(EM_VERTEX_THREADS_HOST)
 for(int i=0;i<2;++i)vertex_workers[i].start=vertex_workers[i].done=-1;
#endif
 vertex_pool_ready=vertex_pool_attempted=0;
}
static int vertex_pool_init(void){
 if(vertex_pool_attempted)return vertex_pool_ready;
 vertex_pool_attempted=1;int failure_rc=-1,failure_worker=-1;const char *failure_stage="host_create";
#if !defined(EM_VERTEX_THREADS_HOST)
 for(int i=0;i<2;++i)vertex_workers[i].start=vertex_workers[i].done=-1;
 int priority=sceKernelGetThreadCurrentPriority();if(priority<0)priority=0x10000100;
#endif
 for(int i=0;i<2;++i){VertexWorker *w=vertex_workers+i;failure_worker=i;
#if defined(EM_VERTEX_THREADS_HOST)
  if(sem_init(&w->start,0,0))goto failed;w->start_ready=1;
  if(sem_init(&w->done,0,0))goto failed;w->done_ready=1;
  if(pthread_create(&w->thread,NULL,vertex_worker_main,w))goto failed;
#else
  w->start=sceKernelCreateSema("emote vertex start",0,0,1,NULL);if(w->start<0){failure_rc=w->start;failure_stage="create_start_sema";goto failed;}
  w->done=sceKernelCreateSema("emote vertex done",0,0,1,NULL);if(w->done<0){failure_rc=w->done;failure_stage="create_done_sema";goto failed;}
  w->priority=priority;w->affinity=i?SCE_KERNEL_CPU_MASK_USER_2:SCE_KERNEL_CPU_MASK_USER_1;
  /* Raw kernel thread deletion skips compiler TLS destructors and newlib
   * record retirement. Model churn must use the complete pthread lifecycle. */
  pthread_attr_t attr;failure_stage="thread_attr";failure_rc=pthread_attr_init(&attr);if(failure_rc)goto failed;
  failure_rc=pthread_attr_setstacksize(&attr,64*1024);
  if(!failure_rc){failure_stage="create_thread";failure_rc=pthread_create(&w->thread,&attr,vertex_worker_main,w);}
  pthread_attr_destroy(&attr);if(failure_rc)goto failed;
  w->started=1;vertex_vita_wait(w->done);
  if(w->setup_result<0){failure_rc=w->setup_result;failure_stage="thread_setup";goto failed;}
#endif
  w->started=1;
 }
 vertex_pool_ready=1;
#if !EM_LOG_DISABLED
 {FILE *f=fopen("ux0:data/emote-workers.txt","a");if(f){fprintf(f,"workers_start count=2 main=1 fp_copy=1");
#if !defined(EM_VERTEX_THREADS_HOST)
 fprintf(f," priority=%d affinity0=0x%x affinity1=0x%x main_affinity=0x%x",priority,SCE_KERNEL_CPU_MASK_USER_1,SCE_KERNEL_CPU_MASK_USER_2,sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()));
#endif
 fprintf(f,"\n");fclose(f);}}
#endif
 return 1;
failed:
 vertex_pool_stop();vertex_pool_attempted=1;
 {FILE *f=fopen("ux0:data/emote-workers.txt","a");if(f){fprintf(f,"workers_start_failed worker=%d stage=%s rc=%d serial_fallback=1 until_model_reload=1\n",failure_worker,failure_stage,failure_rc);fclose(f);}}
 return 0;
}
#else
static void vertex_pool_stop(void){}
#endif
static void vertex_update_all(int width,int height,int profile,VertexJob *stats){
 VertexJob main_job={.view=current_view,.first=0,.last=sprite_count,.width=width,.height=height,.profile=profile};
#if EM_VERTEX_WORKERS
 if(sprite_count>=12&&vertex_pool_init()){
  /* Contiguous ranges balanced by vertex count times deformation-chain depth.
   * Arithmetic is unchanged; estimates only choose which CPU owns each index. */
  unsigned weight[1024],total=0;
  for(int i=0;i<sprite_count;++i){unsigned depth=0;for(int j=sprites[i].m.chain;j>=0;j=deforms[j].next)++depth;weight[i]=sprite_grid_vertices(sprites+i)*(depth+1);total+=weight[i];}
  int cut[4]={0,0,0,sprite_count};unsigned sum=0;int part=1;
  for(int i=0;i<sprite_count-1&&part<3;++i){sum+=weight[i];if(sum>=total*(unsigned)part/3)cut[part++]=i+1;}
  if(!cut[1])cut[1]=sprite_count/3;if(cut[2]<=cut[1])cut[2]=cut[1]+1;if(cut[2]>=sprite_count)cut[2]=sprite_count-1;
#if !defined(EM_VERTEX_THREADS_HOST)
  /* Workers own CPU1/2. Keep the main third off those cores only while
   * they run; respect hosts whose affinity excludes CPU0 and restore it. */
  SceUID owner=sceKernelGetThreadId();int previous_affinity=sceKernelGetThreadCpuAffinityMask(owner);
  int restore_affinity=previous_affinity>0&&(previous_affinity&SCE_KERNEL_CPU_MASK_USER_0)&&previous_affinity!=SCE_KERNEL_CPU_MASK_USER_0&&
   sceKernelChangeThreadCpuAffinityMask(owner,SCE_KERNEL_CPU_MASK_USER_0)>=0;
#endif
  for(int i=0;i<2;++i){VertexWorker *w=vertex_workers+i;w->job=(VertexJob){.view=current_view,.first=cut[i],.last=cut[i+1],.width=width,.height=height,.profile=profile};
#if defined(EM_VERTEX_THREADS_HOST)
   fegetenv(&w->job.fp);vertex_sem_signal(&w->start);
#else
   __asm__ volatile("vmrs %0, fpscr":"=r"(w->job.fpscr)::"memory");vertex_vita_signal(w->start);
#endif
  }
  main_job.first=cut[2];vertex_update_range(&main_job);*stats=main_job;
  for(int i=0;i<2;++i){VertexWorker *w=vertex_workers+i;
#if defined(EM_VERTEX_THREADS_HOST)
   vertex_sem_wait(&w->done);
#else
   vertex_vita_wait(w->done);
#endif
   for(int k=0;k<6;++k)stats->changes[k]+=w->job.changes[k];
   stats->reused+=w->job.reused;stats->rebuilt+=w->job.rebuilt;stats->geometry_us+=w->job.geometry_us;stats->vertices_us+=w->job.vertices_us;
  }
#if !defined(EM_VERTEX_THREADS_HOST)
  if(restore_affinity){int rc=sceKernelChangeThreadCpuAffinityMask(owner,previous_affinity);if(rc<0)vertex_sync_error("restore_affinity",rc);}
#endif
  return;
 }
#endif
 vertex_update_range(&main_job);*stats=main_job;
}
#if EM_PROFILE_DRAW
static void vertex_profile_log(const VertexJob *main_job){
#if EM_VERTEX_WORKERS
 if(vertex_pool_ready){const VertexJob *a=&vertex_workers[0].job,*b=&vertex_workers[1].job;
  em_log_printf(&draw_profile_log,"ux0:data/emote-draw-profile.txt",sceKernelGetSystemTimeWide,"vertex_jobs main_first=%d main_last=%d main_us=%lld worker0_first=%d worker0_last=%d worker0_us=%lld worker1_first=%d worker1_last=%d worker1_us=%lld geometry_vertices_timing=cpu_sum\n",main_job->first,main_job->last,main_job->wall_us,a->first,a->last,a->wall_us,b->first,b->last,b->wall_us);return;
 }
#endif
 em_log_printf(&draw_profile_log,"ux0:data/emote-draw-profile.txt",sceKernelGetSystemTimeWide,"vertex_jobs main_first=%d main_last=%d main_us=%lld geometry_vertices_timing=cpu_sum\n",main_job->first,main_job->last,main_job->wall_us);
}
#endif
/* Recording is opt-in: ordinary drawing does not allocate or copy packets. */
#define VIEW_OP0(name,op) static int view_##name(void){return capture_packet?em_render_packet_command(capture_packet,op,0,0,0,0):gpu_##name();}
VIEW_OP0(soft_mask_begin,EM_RENDER_SOFT_BEGIN)
VIEW_OP0(soft_mask_positive,EM_RENDER_SOFT_POSITIVE)
VIEW_OP0(soft_mask_negative,EM_RENDER_SOFT_NEGATIVE)
VIEW_OP0(soft_mask_end,EM_RENDER_SOFT_END)
VIEW_OP0(soft_mask_disable,EM_RENDER_SOFT_DISABLE)
#undef VIEW_OP0
static int view_material_mode(unsigned m){return capture_packet?em_render_packet_command(capture_packet,EM_RENDER_MATERIAL,(int32_t)m,0,0,0):gpu_material_mode(m);}
static int view_mask_state(unsigned r,unsigned w){return capture_packet?em_render_packet_command(capture_packet,EM_RENDER_MASK_STATE,r,w,0,0):gpu_mask_state(r,w);}
static int view_hard_mask_reset(unsigned n){return capture_packet?em_render_packet_command(capture_packet,EM_RENDER_HARD_RESET,n,0,0,0):gpu_hard_mask_reset(n);}
static int view_hard_mask_step(unsigned r,unsigned op){return capture_packet?em_render_packet_command(capture_packet,EM_RENDER_HARD_STEP,r,op,0,0):gpu_hard_mask_step(r,op);}
static int view_soft_mask_region(int a,int b,int c,int d){return capture_packet?em_render_packet_command(capture_packet,EM_RENDER_SOFT_REGION,a,b,c,d):gpu_soft_mask_region(a,b,c,d);}
/* Geometry is cached independently of mask pass/state. */
static int submit_cached_sprite(unsigned i){
 Sprite *s=sprites+i;int steps=sprite_grid_steps(s);if(!capture_packet)return gpu_grid(&s->t->gpu,mesh_cache[i].grid,steps);
 SharedTexture *e=s->t->shared;if(!e)return -1;
 EmRenderLease lease={e,texture_lease_retain,texture_lease_release,texture_lease_gpu,{e->identity,(uint32_t)s->t->w,(uint32_t)s->t->h},e->host.upload?texture_lease_prepare:NULL};
 _Static_assert(sizeof(EmRenderVertex)==sizeof(GpuVertex),"record vertex ABI");
 #define CHECK_VERTEX_FIELD(f) _Static_assert(offsetof(EmRenderVertex,f)==offsetof(GpuVertex,f),"record vertex offset")
 CHECK_VERTEX_FIELD(x);CHECK_VERTEX_FIELD(y);CHECK_VERTEX_FIELD(z);CHECK_VERTEX_FIELD(w);CHECK_VERTEX_FIELD(r);CHECK_VERTEX_FIELD(g);CHECK_VERTEX_FIELD(b);CHECK_VERTEX_FIELD(a);CHECK_VERTEX_FIELD(u);CHECK_VERTEX_FIELD(v);
 #undef CHECK_VERTEX_FIELD
 return em_render_packet_grid(capture_packet,&lease,mesh_cache[i].grid,steps);
}
static int submit_mask_chain(unsigned chain,unsigned *reference,unsigned first,unsigned visible){
 unsigned negative=0,positive=0;
 for(unsigned g=chain;g;g=mask_groups[g-1].ancestor){unsigned role=mask_groups[g-1].layer->stencil_type&3;negative|=role==2;positive+=role==1;}
 if(!positive&&!negative){*reference=0;return current_view->mask_mode_data?view_mask_state(0,0)>=0:view_hard_mask_reset(0)>=0;}
 if(positive+negative>127)return 0;
 unsigned count=negative;
 if(current_view->mask_mode_data){
  if(view_soft_mask_begin()<0)return 0;
  /* 811779F8 bounds consecutive packets using the same mask chain. Use their
   * actual deformed cached vertices, not sprite corners, plus one pixel guard.
   * A conservative superset avoids changing pixels or mask sampling resolution. */
  float x0=960,y0=544,x1=0,y1=0;
  for(unsigned i=first;i<visible&&sprites[i].m.clip==chain;++i){unsigned n=sprite_grid_vertices(sprites+i);
   for(unsigned k=0;k<n;++k){GpuVertex*v=mesh_cache[i].grid+k;float x=(v->x+1)*480,y=(1-v->y)*272;
    x0=fminf(x0,x);y0=fminf(y0,y);x1=fmaxf(x1,x);y1=fmaxf(y1,y);}}
  x0=fmaxf(0,fminf(960,floorf(x0)-1));y0=fmaxf(0,fminf(544,floorf(y0)-1));x1=fmaxf(0,fminf(960,ceilf(x1)+1));y1=fmaxf(0,fminf(544,ceilf(y1)+1));
  if(view_soft_mask_region((int)x0,(int)y0,(int)x1,(int)y1)<0)return 0;
 }else if(view_hard_mask_reset(count)<0)return 0;
 for(unsigned pass=1;pass<=2;++pass){
  if(pass==2&&negative){if((current_view->mask_mode_data?view_soft_mask_negative():view_hard_mask_step(count,2))<0)return 0;}
  for(unsigned g=chain;g;g=mask_groups[g-1].ancestor){MaskGroup *group=mask_groups+g-1;if((group->layer->stencil_type&3)!=pass)continue;
   if(pass==1){if((current_view->mask_mode_data?view_soft_mask_positive():view_hard_mask_step(count,1))<0)return 0;++count;}
   for(unsigned k=0;k<group->draw_count;++k)if(submit_cached_sprite(group->first_sprite+k)<0)return 0;
  }
 }
 *reference=count;return !current_view->mask_mode_data||view_soft_mask_end()>=0;
}
/* Original renderer constructor 811A4F2E selects hard (0). Soft is an explicit
 * host choice, independent of PSB stencilType. Only recovered profile1 shaders
 * are supported for soft sampling; unknown material profiles reject soft. */
int model_view_mask_mode(int mode){if(mode<0||mode>1||(mode&&current_view->material_profile!=1))return 0;current_view->mask_mode_data=mode;return 1;}
int model_view_get_mask_mode(void){return current_view->mask_mode_data;}
/* Render-owner counters, read only after all vertex workers have joined. */
static uint64_t pose_profile[6],vertex_parts[3];
void model_view_take_vertex_parts(uint64_t out[3]){if(out){memcpy(out,vertex_parts,sizeof vertex_parts);memset(vertex_parts,0,sizeof vertex_parts);}}
static uint64_t dirty_profile[7];
void model_view_take_dirty_profile(uint64_t out[7]){if(out){memcpy(out,dirty_profile,sizeof dirty_profile);memset(dirty_profile,0,sizeof dirty_profile);}}
void model_view_take_pose_profile(uint64_t out[6]){if(out){memcpy(out,pose_profile,sizeof(pose_profile));memset(pose_profile,0,sizeof(pose_profile));}}
int model_view_evaluate(int width,int height,float eye,float mouth){
 current_view->controls_for_pose=model_controls_context_current();
 unsigned binding=model_controls_anchor_binding_in(current_view->controls_for_pose);
 if(binding!=current_view->anchor_binding){memset(current_view->anchor_demand,0,sizeof current_view->anchor_demand);current_view->anchor_binding=binding;}
 current_view->evaluation_ready=current_view->vertices_ready=0;SceInt64 start=sceKernelGetSystemTimeWide();
 pose_parts_active=samples%120==0;if(pose_parts_active)++pose_parts[0];
 old_deform_count=deform_count;sprite_count=missing=nonneutral=deform_count=0;view_mask_count=mask_write=0;generating_masks=0;current_view->mask_failed_data=0;eye_value=eye;mouth_value=mouth;
 if(++scene_epoch==0){scene_epoch=1;for(unsigned i=0;i<scene.motion_count;++i)for(unsigned j=0;j<scene.motions[i].parameter_count;++j)scene.motions[i].parameters[j].epoch=0;}
 if(!scene.motions||scene.base_motion<0||(unsigned)scene.base_motion>=scene.motion_count)return 0;
 current_view->anchor_instance_count=current_view->anchor_candidate_count=0;current_view->anchor_instance=-1;current_view->anchor_failed=current_view->anchor_only=0;
 model_controls_begin_pose();
 current_view->event_instance=-1;
 motion_scene(current_view,scene.motions+scene.base_motion,identity(),current_view->motion_clock,0,-1,NULL);
 uint64_t final_part=pose_mark(current_view);event_publish();pose_add(current_view,6,final_part);
 final_part=pose_mark(current_view);anchor_publish(current_view);pose_add(current_view,7,final_part);
 model_controls_finish_pose();
 /* Stable depth ordering, including sprites emitted by referenced motions. */
 for(int i=1;i<sprite_count;++i){Sprite s=sprites[i];int j=i;while(j>0&&sprites[j-1].m.z>s.m.z){sprites[j]=sprites[j-1];--j;}sprites[j]=s;}
 int visible_sprites=sprite_count;
 for(int i=0;i<visible_sprites;++i)for(unsigned g=sprites[i].m.clip;g;g=mask_groups[g-1].ancestor){mask_groups[g-1].used=1;}
 /* Presentation only: apply after model-space evaluation and before packing.
  * Reassert on every draw, including after free/load clears fitted. Existing
  * exact fit-bit cache keys invalidate positions without touching UV/color. */
 if(current_view->projection_explicit){fit_scale=current_view->projection_scale;fit_x=current_view->projection_x;fit_y=current_view->projection_y;fitted=1;}
 float minx=1e9f,miny=1e9f,maxx=-1e9f,maxy=-1e9f;
 if(!fitted)for(int i=0;i<sprite_count;++i)for(int j=0;j<4;++j){float x,y;corner(current_view,sprites+i,(j&1)?sprites[i].w:0,(j&2)?sprites[i].h:0,&x,&y);minx=fminf(minx,x);miny=fminf(miny,y);maxx=fmaxf(maxx,x);maxy=fmaxf(maxy,y);}
 if(!fitted&&sprite_count&&current_view->box_w>0&&current_view->box_h>0){fit_scale=fminf(current_view->box_w/fmaxf(maxx-minx,1),current_view->box_h/fmaxf(maxy-miny,1));fit_x=current_view->box_x+current_view->box_w*.5f-(minx+maxx)*.5f*fit_scale;fit_y=current_view->box_y+current_view->box_h*.5f-(miny+maxy)*.5f*fit_scale;fitted=1;}
 if(!fitted&&sprite_count){fit_scale=fminf((width-360)/(maxx-minx),(height-80)/(maxy-miny));fit_x=width*.62f-(minx+maxx)*.5f*fit_scale;fit_y=height*.5f-(miny+maxy)*.5f*fit_scale;fitted=1;}
 /* Compile each used mask's geometry once. Composite members form one node;
  * original packet traversal rebuilds the counter/alpha chain when it changes. */
 generating_masks=1;
 for(unsigned g=0;g<view_mask_count;++g){MaskGroup *group=mask_groups+g;if(!group->used)continue;mask_write=g+1;group->first_sprite=sprite_count;current_view->event_instance=group->owner_event;
  for(unsigned k=0;k<group->count;++k)emit(current_view,group->targets+k,group->depth);
  group->draw_count=sprite_count-group->first_sprite;
 }
 generating_masks=0;mask_write=0;current_view->event_instance=-1;
 if(current_view->mask_failed_data){snprintf(status,sizeof(status),"MASK EVALUATION FAILED");return 0;}
 current_view->visible_sprites_data=visible_sprites;
 current_view->evaluation_us=sceKernelGetSystemTimeWide()-start;current_view->evaluation_ready=1;return 1;
}
static void model_view_finish(int width,int height){
 if(!current_view->evaluation_ready)return;current_view->evaluation_ready=0;
 int visible_sprites=current_view->visible_sprites_data;
 SceInt64 start=sceKernelGetSystemTimeWide()-current_view->evaluation_us;
 SceInt64 evaluated=sceKernelGetSystemTimeWide();
 /* Reuse only bit-identical geometry inputs, including the entire current
  * deformation chain. This is an adapter cache, not a guessed original flag.
  * Vertex density and recovered patch arithmetic are unchanged. */
 unsigned reused=0,rebuilt=0;int profile=0;
#if EM_PROFILE_DRAW
 profile=samples==0;SceInt64 geometry_us=0,vertices_us=0,submit_us=0,mark=0;
#endif
 int prepared=current_view->vertices_ready&&current_view->vertices_width==width&&current_view->vertices_height==height;
 VertexJob stats={0};if(prepared){const EmViewVertexStats *v=&current_view->prepared_vertices;
  stats.reused=v->reused;stats.rebuilt=v->rebuilt;memcpy(stats.changes,v->changes,sizeof(stats.changes));stats.geometry_us=v->geometry_us;stats.vertices_us=v->vertices_us;
 }else vertex_update_all(width,height,profile,&stats);
 current_view->vertices_ready=0;
 reused=stats.reused;rebuilt=stats.rebuilt;
 if(stats.geometry_us||stats.vertices_us){++vertex_parts[0];vertex_parts[1]+=stats.geometry_us;vertex_parts[2]+=stats.vertices_us;}
 ++dirty_profile[0];for(int k=0;k<6;++k)dirty_profile[k+1]+=stats.changes[k];
#if EM_PROFILE_DRAW
 geometry_us=stats.geometry_us;vertices_us=stats.vertices_us;
#endif
 SceInt64 vertices_done=sceKernelGetSystemTimeWide();
 SceInt64 vertex_wall=prepared?current_view->prepared_vertices.wall_us:vertices_done-evaluated;
 /* Vertex workers have joined. */
 /* Reserve the ordinary pass once; repeated stencil submissions can still grow. */
 if(capture_packet){uint32_t nv=0;for(int i=0;i<sprite_count;++i){int steps=sprite_grid_steps(sprites+i);nv+=(steps+1)*(steps+1);}
  const uint32_t ni=6+6*grid_steps()*grid_steps(); /* One owned index span per fixed topology. */
  if(em_render_packet_reserve(capture_packet,(uint32_t)sprite_count*3+1,nv,ni)<0){++missing;return;}
 }

#if EM_PROFILE_DRAW
 if(profile)mark=sceKernelGetSystemTimeWide();
#endif
 unsigned active_chain=UINT32_MAX,reference=0;
 for(int i=0;i<visible_sprites;++i){Sprite *s=sprites+i;
  if(s->m.clip!=active_chain){
   if(current_view->mask_mode_data&&view_soft_mask_disable()<0){++missing;break;}
   active_chain=s->m.clip;
   if(active_chain){if(!submit_mask_chain(active_chain,&reference,(unsigned)i,(unsigned)visible_sprites)){++missing;break;}}
  }
  if(view_material_mode(s->original_material?s->blend_mode:UINT32_MAX)<0||
     (active_chain&&!current_view->mask_mode_data?view_hard_mask_step(reference,0):view_mask_state(0,0))<0||submit_cached_sprite(i)<0)++missing;
 }
 if(current_view->mask_mode_data)view_soft_mask_disable();else view_mask_state(0,0);
#if EM_PROFILE_DRAW
 if(profile)submit_us+=sceKernelGetSystemTimeWide()-mark;
#endif

#if EM_PROFILE_DRAW
 if(profile){em_log_printf(&draw_profile_log,"ux0:data/emote-draw-profile.txt",sceKernelGetSystemTimeWide,"action=%d sprites=%d rebuilt=%u geometry_us=%lld vertices_us=%lld submit_with_indices_us=%lld\n",timeline_index(),sprite_count,rebuilt,geometry_us,vertices_us,submit_us);if(!prepared)vertex_profile_log(&stats);}
#endif
 snprintf(status,sizeof(status),"%d SPRITES  %d MISSING  %d MESH",sprite_count,missing,nonneutral);
 SceInt64 submitted=sceKernelGetSystemTimeWide();
 ++pose_profile[0];pose_profile[1]+=evaluated-start;pose_profile[2]+=vertex_wall;
 pose_profile[3]+=submitted-vertices_done;pose_profile[4]+=sprite_count;pose_profile[5]+=rebuilt;
 eval_sum+=evaluated-start;draw_sum+=submitted-vertices_done+vertex_wall;
 if(++samples==120){em_log_printf(&performance_log,"ux0:data/emote-performance.txt",sceKernelGetSystemTimeWide,"backend=gxm action=%d eval_us=%lld draw_us=%lld sprites=%d mesh=%d reused=%u rebuilt=%u\n",timeline_index(),eval_sum/samples,draw_sum/samples,sprite_count,nonneutral,reused,rebuilt);samples=0;eval_sum=draw_sum=0;}
}
void model_view_draw(uint32_t *pixels,int width,int height,float eye,float mouth){
 (void)pixels;if(current_view->texture_host.upload&&!capture_packet)return;
 if(model_view_evaluate(width,height,eye,mouth))model_view_finish(width,height);
}
const char *model_view_status(void){return status;}

EmRenderPacket *model_view_capture(float eye,float mouth){
 if(capture_packet||!scene.motions||scene.base_motion<0||(unsigned)scene.base_motion>=scene.motion_count)return NULL;
 EmRenderPacket *p=em_render_packet_create();if(!p)return NULL;capture_packet=p;
 model_view_draw(NULL,960,544,eye,mouth);capture_packet=NULL;
 if(missing||current_view->mask_failed_data||current_view->event_failed||!em_render_packet_seal(p)){em_render_packet_destroy(p);return NULL;}return p;
}

EmRenderPacket *model_view_capture_evaluated(void){
 if(capture_packet||!current_view->evaluation_ready)return NULL;
 EmRenderPacket *p=em_render_packet_create();if(!p){current_view->evaluation_ready=0;return NULL;}capture_packet=p;
 model_view_finish(960,544);capture_packet=NULL;
 if(missing||current_view->mask_failed_data||current_view->event_failed||!em_render_packet_seal(p)){em_render_packet_destroy(p);return NULL;}return p;
}
