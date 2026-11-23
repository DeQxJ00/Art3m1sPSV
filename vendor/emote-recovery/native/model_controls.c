/* Model metadata adapter. Original evidence in reverse/controller-integration/original.
 * Blink stages: 81186C58. Mouth base/talk separation: 81188B30.
 * Physics equations: 8118601C / 8118923C. Physics substeps/anchor interpolation:
 * 811837A4 / 81183A00. Eye/loop subdivision, test speech envelope and optional
 * wind settings remain demo policy; full engine scheduling is not recovered. */
#include "model_controls.h"
#include "timeline.h"
#include "controls.h"
#include "physics_schedule.h"
#include "wind.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
typedef EmPsbValue V;
static V get(V v,const char *s){V r={0};if(v.data&&*v.data==33)em_psb_find(&v,(const uint8_t*)s,&r);return r;}
static unsigned count(V v){return v.data&&(*v.data==32||*v.data==33)?em_psb_array(v.data+1).count:0;}
static V at(V v,unsigned i){V r={0};if(i<count(v))em_psb_at(&r,&v,i);return r;}
static float num(V v,float d){return v.data&&*v.data!=1?em_psb_float(&v):d;}
static float prop(V v,const char *k,float d){return num(get(v,k),d);}
static const char *str(V v){return v.data&&*v.data>=21&&*v.data<=24?em_psb_string(&v):"";}
static float clamp(float x,float a,float b){return fminf(b,fmaxf(a,x));}
typedef struct {const char *id;float lo,hi,value;int stereo_target;} Range;
typedef struct {const char *id;float begin,end,frames,min,max,wait,blink;int state;} Eye;
typedef struct {const char *id;float lo,hi;} Mouth;
static const EmLoopSegment syllables[]={{0,1,4},{1,.2f,5},{.2f,.8f,3},{.8f,0,6},{0,0,8}};
typedef struct {const char *a,*b;float lo,hi;int type;} Clamp;
typedef struct {const char *label,*lr,*ud,*lrm;int hair,group,ready,anchored,seen,ud_eft;float x,y,previous_x,previous_y,ox,oy,offset_x,offset_y,ofs;
 float p[2][2],v[2][2],length[2],sx[2],sy[2],gravity,friction_x,friction_y,spring,b_rate,v_bound;
 float bend_speed,bend_volume,bend_phase,bend_strength;float output[3];} Physics;
struct EmControlsContext {
 Range range_data[512];unsigned range_count;
 uint16_t range_index[1024]; /* index + 1, zero is an empty bucket */
 Eye eye_data[8];unsigned eye_count;
 Mouth mouth_data[8];unsigned mouth_count;
 int talk_data,wind_data,open_data;float amount_data;
 EmRandom random_data;EmWind wind_state;EmLoop speech_state;
 float wind_start,wind_end;
 Clamp clamp_data[32];unsigned clamp_count;
 Physics initial_physics[32],physics_data[32];unsigned physics_count;
 unsigned anchor_binding;
 float physics_pending;int collecting_pose;
 float origin[2],inverse_scale,gravity_direction[2],output_scale[3];
 float pose_angle,outer_rotation,outer_force[3][2];
 float authored_scale;
 uint32_t suitable_clear_color;
 int blink_disabled;
 V stereo_variables;
 float stereo_coefficient,stereo_anchor_ratio;
};
/* Binding serials distinguish even recycled context addresses. Binding is
 * outside frozen evaluation; no per-frame atomic operations are needed. */
static atomic_uint next_anchor_binding=1;
static EmControlsContext default_controls;
static __thread EmControlsContext *current_controls=&default_controls;
#define ranges (current_controls->range_data)
#define nr (current_controls->range_count)
#define eyes (current_controls->eye_data)
#define ne (current_controls->eye_count)
#define mouths (current_controls->mouth_data)
#define nm (current_controls->mouth_count)
#define talking (current_controls->talk_data)
#define windy (current_controls->wind_data)
#define mouth_open (current_controls->open_data)
#define mouth_amount (current_controls->amount_data)
#define random_ (current_controls->random_data)
#define wind (current_controls->wind_state)
#define speech (current_controls->speech_state)
#define clamps (current_controls->clamp_data)
#define nc (current_controls->clamp_count)
#define physics (current_controls->physics_data)
#define np (current_controls->physics_count)
EmControlsContext *model_controls_context_create(void){return calloc(1,sizeof(EmControlsContext));}
EmControlsContext *model_controls_context_use(EmControlsContext *context){
 EmControlsContext *previous=current_controls;current_controls=context?context:&default_controls;return previous;
}
void model_controls_context_destroy(EmControlsContext *context){
 if(!context||context==&default_controls)return;
 if(current_controls==context)current_controls=&default_controls;
 free(context);
}
static void update_gravity(EmControlsContext *context){
 float degrees=context->pose_angle;
 if(context->outer_rotation!=0)degrees+=context->outer_rotation;
 float angle=-em_physics_angle_radians(degrees);
 /* Original ELF gravity basis at 812D8BC4 is (0,1,0). */
 em_physics_rotate_gravity(context->gravity_direction,0,1,sinf(angle),cosf(angle));
}
void model_controls_assign(EmControlsContext *target,const EmControlsContext *source,int same_model){
 target->wind_data=source->wind_data;target->wind_state=source->wind_state;
 target->random_data=source->random_data;
 target->wind_start=source->wind_start;target->wind_end=source->wind_end;
 target->talk_data=source->talk_data;target->open_data=source->open_data;
 target->amount_data=source->amount_data;target->speech_state=source->speech_state;
 target->inverse_scale=source->inverse_scale;
 memcpy(target->output_scale,source->output_scale,sizeof(target->output_scale));
 memcpy(target->outer_force,source->outer_force,sizeof(target->outer_force));
 target->outer_rotation=source->outer_rotation;
 update_gravity(target);
 target->blink_disabled=source->blink_disabled;
 target->stereo_coefficient=source->stereo_coefficient;
 target->stereo_anchor_ratio=source->stereo_anchor_ratio;
 for(unsigned i=0;i<target->eye_count;++i){
  Eye *eye=target->eye_data+i;
  for(unsigned j=0;j<source->eye_count;++j){
   const Eye *old=source->eye_data+j;
   if(strcmp(eye->id,old->id))continue;
   eye->wait=old->wait;eye->blink=clamp(old->blink,eye->begin,eye->end);
   eye->state=old->state;break;
  }
 }
 for(unsigned i=0;i<target->physics_count;++i){
  Physics *part=target->physics_data+i;
  for(unsigned j=0;j<source->physics_count;++j){
   const Physics *old=source->physics_data+j;
   if(part->group!=old->group||strcmp(part->label,old->label)||
      strcmp(part->lr,old->lr)||strcmp(part->ud,old->ud)||strcmp(part->lrm,old->lrm))continue;
   memcpy(part->p,old->p,sizeof(part->p));memcpy(part->v,old->v,sizeof(part->v));
   memcpy(part->output,old->output,sizeof(part->output));
   part->bend_phase=old->bend_phase;part->bend_strength=old->bend_strength;
   if(same_model){
    part->ready=old->ready;part->anchored=old->anchored;
    part->x=old->x;part->y=old->y;part->previous_x=old->previous_x;part->previous_y=old->previous_y;
    part->offset_x=old->offset_x;part->offset_y=old->offset_y;
   }else{
    part->ready=part->anchored=0;
   }
   break;
  }
 }
 target->physics_pending=same_model?source->physics_pending:0;
}
/* Name-only index: values remain live, and absent names still query timeline.
 * At most 512 entries in 1024 slots; range registration keeps the first id. */
static unsigned range_hash(const char *id){unsigned h=2166136261u;for(const unsigned char *s=(const unsigned char*)id;*s;++s)h=(h^*s)*16777619u;return h&1023u;}
static unsigned range_slot_in(const EmControlsContext *context,const char *id){unsigned slot=range_hash(id);while(context->range_index[slot]&&strcmp(context->range_data[context->range_index[slot]-1].id,id))slot=(slot+1)&1023u;return slot;}
static unsigned range_slot(const char *id){return range_slot_in(current_controls,id);}
EmControlsContext *model_controls_context_current(void){return current_controls;}
static float rand01(void){unsigned t=random_.x^(random_.x<<11),w=t^(t>>8)^random_.w^(random_.w>>19);random_.x=random_.y;random_.y=random_.z;random_.z=random_.w;random_.w=w;return (w&0x7fffff)/8388608.0f;}
static uint32_t seed_word(uint64_t *seed){
 /* SplitMix64 expands the host instance identity; this is adapter seeding
  * policy, not a recovered native entropy source. */
 uint64_t z=(*seed+=UINT64_C(0x9e3779b97f4a7c15));
 z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
 z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
 return (uint32_t)(z^(z>>31));
}
void model_controls_seed(uint64_t seed){
 random_.x=seed_word(&seed);random_.y=seed_word(&seed);
 random_.z=seed_word(&seed);random_.w=seed_word(&seed);
 if(!(random_.x|random_.y|random_.z|random_.w))random_.w=1;
 for(unsigned i=0;i<ne;++i){Eye *e=eyes+i;e->wait=e->min+(e->max-e->min)*rand01();}
}
static float wind_at(float x){for(int i=0;i<128;++i){EmWindParticle *p=wind.particles+i;float radius=(p->amplitude*.5f)*4;if(p->active&&x>p->position-radius&&x<p->position+radius)return p->amplitude*(wind.speed>=0?1:-1);}return 0;}
static void bind_physics(V list,int group){int hair=group!=0;for(unsigned i=0;i<count(list)&&np<32;++i){V v=at(list,i);if(!prop(v,"enabled",0))continue;Physics *p=physics+np++;memset(p,0,sizeof(*p));p->hair=hair;p->group=group;
 p->label=str(get(v,"baseLayer"));p->lr=str(get(v,"var_lr"));p->ud=str(get(v,"var_ud"));p->lrm=str(get(v,"var_lrm"));p->ud_eft=prop(v,"ud_eft",0);
 p->gravity=prop(v,"gravity",0);p->spring=prop(v,"spring",0);p->b_rate=prop(v,"b_rate",0);p->v_bound=prop(v,"v_bound",0);
 p->friction_x=prop(v,hair?"friction_x":"friction",0);p->friction_y=prop(v,hair?"friction_y":"friction",0);
 V param=get(v,"param"),origin=get(param,"op");p->ox=prop(origin,"x",0);p->oy=prop(origin,"y",0);p->ofs=prop(param,"ofs",0);
 p->bend_speed=prop(v,"bend_spd",0);p->bend_volume=prop(v,"bend_vol",0);p->bend_phase=prop(param,"bendR",0);p->bend_strength=prop(param,"bendS",0);
 for(int j=0;j<(hair?2:1);++j){V pos=hair?at(get(param,"p"),j):get(param,"p"),vel=hair?at(get(param,"pv"),j):get(param,"pv");
  p->p[j][0]=prop(pos,"x",0);p->p[j][1]=prop(pos,"y",0);p->v[j][0]=prop(vel,"x",0);p->v[j][1]=prop(vel,"y",0);
  p->length[j]=num(at(get(v,"length"),j),1);p->sx[j]=hair?num(at(get(v,"scale_x"),j),1):prop(v,"scale_x",1);p->sy[j]=hair?num(at(get(v,"scale_y"),j),1):prop(v,"scale_y",1);
 }
}}
void model_controls_set_physics_scales(float inverse_scale,float bust_scale,float hair_scale,float parts_scale){
 current_controls->inverse_scale=inverse_scale;
 current_controls->output_scale[0]=bust_scale;current_controls->output_scale[1]=hair_scale;current_controls->output_scale[2]=parts_scale;
}
void model_controls_set_physics_scale(unsigned group,float scale){
 current_controls->output_scale[group]=scale;
}
float model_controls_physics_scale(unsigned group){
 return current_controls->output_scale[group];
}
void model_controls_pose_root(float origin_x,float origin_y,float angle_degrees){
 current_controls->origin[0]=origin_x;current_controls->origin[1]=origin_y;
 current_controls->pose_angle=angle_degrees;
 update_gravity(current_controls);
}
void model_controls_set_outer_force(unsigned group,float x,float y){
 if(group>=3)return;
 current_controls->outer_force[group][0]=x;
 current_controls->outer_force[group][1]=y;
}
void model_controls_set_outer_rotation(float degrees){
 if(current_controls->outer_rotation==degrees)return;
 current_controls->outer_rotation=degrees;
 update_gravity(current_controls);
}
void model_controls_set_stereovision(int enabled,float coefficient,unsigned index,unsigned screen_count){
 current_controls->stereo_coefficient=0;
 if(!enabled||!isfinite(coefficient)||index>=screen_count||screen_count<2)return;
 current_controls->stereo_coefficient=coefficient;
 current_controls->stereo_anchor_ratio=(float)(screen_count-index-1)/(float)(screen_count-1);
}
uint32_t model_controls_suitable_clear_color(void){return current_controls->suitable_clear_color;}
float model_controls_authored_scale(void){return current_controls->authored_scale;}
void model_controls_set_physics_host(float origin_x,float origin_y,float inverse_scale,
                                    float angle_degrees,float bust_scale,float hair_scale,float parts_scale){
 model_controls_set_physics_scales(inverse_scale,bust_scale,hair_scale,parts_scale);
 model_controls_pose_root(origin_x,origin_y,angle_degrees);
}
void model_controls_bind(V root){
 current_controls->anchor_binding=atomic_fetch_add_explicit(&next_anchor_binding,1,memory_order_relaxed);
 current_controls->outer_rotation=0;
 memset(current_controls->outer_force,0,sizeof(current_controls->outer_force));
 current_controls->blink_disabled=0;
 model_controls_set_stereovision(0,0,0,0);
 model_controls_set_physics_host(0,0,1,0,1,1,1);
 current_controls->physics_pending=0;current_controls->collecting_pose=0;
 nr=ne=nm=nc=np=0;
 memset(current_controls->range_index,0,sizeof current_controls->range_index);
 talking=windy=mouth_open=0;mouth_amount=0;
 random_=(EmRandom){123456789,362436069,521288629,88675123};
 em_wind_init(&wind,-1000,1000);speech=(EmLoop){.begin=syllables,.end=syllables+5,.capacity_end=syllables+5};
 V meta=get(root,"metadata"),list=get(meta,"eyeControl");
 current_controls->authored_scale=prop(meta,"scale",1);
 V color=get(meta,"suitableClearColor");
 V red=get(color,"r"),green=get(color,"g"),blue=get(color,"b");
 current_controls->suitable_clear_color=0;
 if(red.data&&green.data&&blue.data){
  current_controls->suitable_clear_color=(uint32_t)em_psb_int(&red)<<24 |
      (uint32_t)em_psb_int(&green)<<16 | (uint32_t)em_psb_int(&blue)<<8;
 }
 current_controls->stereo_variables=get(get(meta,"stereovisionControl"),"variableMatchList");
 for(unsigned i=0;i<count(list)&&ne<8;++i){V v=at(list,i);if(!prop(v,"enabled",0)||!prop(v,"blinkEnabled",0))continue;Eye *e=eyes+ne++;*e=(Eye){.id=str(get(v,"label")),.begin=prop(v,"beginFrame",0),.end=prop(v,"endFrame",10),.frames=prop(v,"blinkFrameCount",16),.min=prop(v,"blinkIntervalMin",120),.max=prop(v,"blinkIntervalMax",240)};e->blink=e->begin;e->wait=e->min+(e->max-e->min)*rand01();}
 list=get(meta,"mouthControl");for(unsigned i=0;i<count(list)&&nm<8;++i){V v=at(list,i);if(prop(v,"enabled",0))mouths[nm++]=(Mouth){str(get(v,"talkLabel")),0,0};}
 list=get(meta,"clampControl");for(unsigned i=0;i<count(list)&&nc<32;++i){V v=at(list,i);if(prop(v,"enabled",0))clamps[nc++]=(Clamp){str(get(v,"var_lr")),str(get(v,"var_ud")),prop(v,"min",-30),prop(v,"max",30),(int)prop(v,"type",0)};}
 bind_physics(get(meta,"bustControl"),0);bind_physics(get(meta,"hairControl"),1);bind_physics(get(meta,"partsControl"),2);
 memcpy(current_controls->initial_physics,physics,np*sizeof(*physics));
}
void model_controls_reset_physics(void){
 /* Original player Step/Skip restores metadata initial pose/velocity, marks
  * anchors dirty and writes zero feedback; autonomous blink/talk survives. */
 memcpy(physics,current_controls->initial_physics,np*sizeof(*physics));
 current_controls->physics_pending=0;current_controls->collecting_pose=0;
 for(unsigned i=0;i<np;++i){Physics*p=physics+i;timeline_publish_control(p->lr,0);timeline_publish_control(p->ud,0);if(*p->lrm)timeline_publish_control(p->lrm,0);}
}
void model_controls_range(const char *id,float lo,float hi){
 if(!*id)return;
 unsigned slot=range_slot(id);
 if(current_controls->range_index[slot])return;
 if(nr<512){
  V list=current_controls->stereo_variables;
  int stereo_target=0;
  for(unsigned i=0;i<count(list);++i){
   if(!strcmp(id,str(at(list,i)))){stereo_target=1;break;}
  }
  ranges[nr]=(Range){.id=id,.lo=lo,.hi=hi,.stereo_target=stereo_target};
  current_controls->range_index[slot]=(uint16_t)(++nr);
 }
 for(unsigned i=0;i<nm;++i)if(!strcmp(mouths[i].id,id)){mouths[i].lo=lo;mouths[i].hi=hi;}
}
unsigned model_controls_anchor_binding_in(const EmControlsContext *context){return (context?context:current_controls)->anchor_binding;}
int model_controls_needs_anchor_in(const EmControlsContext *context,const char *label){
 if(!context)context=current_controls;
 for(unsigned i=0;i<context->physics_count;++i)if(!strcmp(context->physics_data[i].label,label))return 1;
 return 0;
}
int model_controls_needs_anchor(const char *label){return model_controls_needs_anchor_in(current_controls,label);}
void model_controls_begin_pose(void){current_controls->collecting_pose=1;for(unsigned i=0;i<np;++i)physics[i].seen=0;}
void model_controls_anchor_candidate(const char *label,float x,float y,int valid){
 if(!current_controls->collecting_pose)return;
 for(unsigned i=0;i<np;++i){Physics *p=physics+i;if(!p->seen&&!strcmp(p->label,label)){
  /* 8119737E returns the first named layer; 811836FA rejects unsuitable
   * type/shape without searching later duplicate labels. Keep old anchor. */
  p->seen=1;if(valid){float a[2];em_physics_normalize_anchor(a,x,y,current_controls->origin[0],current_controls->origin[1],current_controls->inverse_scale);p->x=a[0];p->y=a[1];p->anchored=1;}
 }}
}
void model_controls_anchor(const char *label,float x,float y){model_controls_anchor_candidate(label,x,y,1);}
static void physics_step(Physics *p,float dt){if(!p->anchored)return;if(!p->ready){p->offset_x=p->ox-p->x;p->offset_y=p->oy-p->y;p->ready=1;}
 float x=p->x+p->offset_x,y=p->y+p->offset_y;
 if(p->hair){
  const float *force=current_controls->outer_force[p->group];
  x+=force[0];y+=force[1];
 }
 float output_scale=current_controls->output_scale[p->group];float gx=current_controls->gravity_direction[0],gy=current_controls->gravity_direction[1];
 if(!p->hair){float pos[3]={p->p[0][0],p->p[0][1],0},vel[3]={p->v[0][0],p->v[0][1],0},anchor[3]={x,y,0};
  em_bust_spring(pos,vel,anchor,p->spring,p->friction_x,p->gravity,gx,gy,dt);
  p->p[0][0]=pos[0];p->p[0][1]=pos[1];p->v[0][0]=vel[0];p->v[0][1]=vel[1];
  p->output[0]=em_physics_limit(-((x-p->p[0][0])*output_scale)*p->sx[0]);p->output[2]=em_physics_limit((-(y-p->p[0][1])*output_scale-p->ofs)*p->sy[0]);return;
 }
 float basey=y;
 for(int j=0;j<2;++j){float ax=j?p->p[0][0]:x,ay=j?p->p[0][1]:y;float dx=ax-p->p[j][0],dy=ay-p->p[j][1];float d=sqrtf(dx*dx+dy*dy),length=p->length[j];
  if(d>length&&d>.015625f){dx/=d;dy/=d;float correction;
   if(j){p->p[j][0]+=(d-length)*dx;p->p[j][1]+=(d-length)*dy;float dot=dx*p->v[j][0]+dy*p->v[j][1];correction=-dot*p->v_bound*dt;}
   else correction=(d-length)*p->b_rate*dt;
   p->v[j][0]+=correction*dx;p->v[j][1]+=correction*dy;
  }
  float gravity_step=p->gravity*dt;p->v[j][0]+=gx*gravity_step;p->v[j][1]+=gy*gravity_step;if(windy)p->v[j][0]+=wind_at(p->p[j][0]);
  p->v[j][0]-=p->v[j][0]*p->friction_x*dt;p->v[j][1]-=p->v[j][1]*p->friction_y*dt;
  p->p[j][0]+=dt*p->v[j][0];p->p[j][1]+=dt*p->v[j][1];basey+=length;
  p->output[j]=em_physics_limit(-((x-p->p[j][0])*p->sx[j])*output_scale);if(j==p->ud_eft)p->output[2]=em_physics_limit(((p->ofs-(basey-p->p[j][1]))*p->sy[j])*output_scale);
 }
 /* Original 81189634 calls 8118916A after the spring step: the vertical
  * excursion drives a bounded oscillation added oppositely to LR/LR_M. */
 p->bend_strength=clamp(p->bend_strength+(fabsf(p->output[2])>28?1:-1)*(dt*.03125f),0,1);
 p->bend_phase=fmodf(p->bend_phase+(p->bend_speed*p->bend_strength)*dt,6.2831854820251465f);
 float bend=(sinf(p->bend_phase)*p->bend_strength)*p->bend_volume;
 p->output[1]+=bend;p->output[0]-=bend;
}
void model_controls_finish_pose(void){
 if(!current_controls->collecting_pose)return;
 current_controls->collecting_pose=0;
 float frames=current_controls->physics_pending;current_controls->physics_pending=0;
 if(frames<=0)return;
 for(unsigned i=0;i<np;++i){Physics *p=physics+i;if(!p->anchored)continue;
  float x=p->x,y=p->y,dt,anchor[2];
  const float *force=current_controls->outer_force[p->group];
  EmPhysicsSchedule schedule={.frames=frames,.previous={p->previous_x,p->previous_y},.current={x,y},.first=!p->ready};
  /* Bust interpolates toward the forced endpoint. Hair/parts apply the
   * force to every interpolated anchor after capturing the base offset. */
  if(!p->hair){
   schedule.current[0]+=force[0];schedule.current[1]+=force[1];
  }
  while(em_physics_schedule_next(&schedule,&dt,anchor)){p->x=anchor[0];p->y=anchor[1];physics_step(p,dt);}
  p->x=p->previous_x=x;p->y=p->previous_y=y;
  /* 81183928..88 / 81183B76..B6: only snap when every output is tiny. */
  if(fabsf(p->output[0])<=.01f&&fabsf(p->output[1])<=.01f&&fabsf(p->output[2])<=.01f)
   p->output[0]=p->output[1]=p->output[2]=0;
  /* 811796DA writes the shared variable map after model evaluation. Next
   * update can overwrite it with normal controls before adding diff tracks. */
  timeline_publish_control(p->lr,p->output[0]);
  if(*p->lrm)timeline_publish_control(p->lrm,p->output[1]);
  timeline_publish_control(p->ud,p->output[2]);
 }
}
void model_controls_step(float frames){frames=clamp(frames,0,15);
 current_controls->physics_pending+=frames;
 if(mouth_open)mouth_amount=1;else if(talking)em_loop_step(&speech,&mouth_amount,frames);else mouth_amount=0;
 unsigned blink_eyes=current_controls->blink_disabled?0:ne;
 for(float left=frames;left>0;){float dt=fminf(1,left);left-=dt;if(windy)em_wind_step(&wind,&random_,dt);
  for(unsigned i=0;i<blink_eyes;++i){Eye *e=eyes+i;if(e->frames<=0||e->end<=e->begin)continue;
   if(e->state==10){e->blink+=(e->end-e->begin)*(dt*2.5f/e->frames);if(e->blink>=e->end){e->blink=e->end;e->state=11;e->wait=e->frames/5;}}
   else if(e->state==11){e->wait-=dt;if(e->wait<=0){e->state=12;e->wait=e->min+(e->max-e->min)*rand01();}}
   else if(e->state==12){e->blink-=(e->end-e->begin)*(dt*2.5f/e->frames);if(e->blink<=e->begin){e->blink=e->begin;e->state=0;}}
   else {e->wait-=dt;if(e->wait<=0)e->state=10;}
  }
 }
 for(unsigned i=0;i<nr;++i){Range *r=ranges+i;r->value=timeline_value(r->id);
  for(unsigned j=0;j<blink_eyes;++j){Eye *e=eyes+j;if(!strcmp(r->id,e->id)&&r->value>=e->begin&&r->value<=e->end&&e->end>e->begin)r->value+=(e->blink-e->begin)*(e->end-r->value)/(e->end-e->begin);}
  if(talking||mouth_open)for(unsigned j=0;j<nm;++j)if(!strcmp(r->id,mouths[j].id))r->value=mouths[j].lo+(mouths[j].hi-mouths[j].lo)*mouth_amount;
 }
 /* 81184388 / 8118465C: paired controls are NOT independent axis clamps.
  * Type 0 remaps diagonals smoothly; type 1 constrains the unit circle. */
 for(unsigned j=0;j<nc;++j){Clamp *c=clamps+j;Range *a=NULL,*b=NULL;
  for(unsigned i=0;i<nr;++i){if(!strcmp(ranges[i].id,c->a))a=ranges+i;if(!strcmp(ranges[i].id,c->b))b=ranges+i;}
  if(!a||!b||c->hi<=c->lo)continue;
  float span=c->hi-c->lo,x=((a->value-c->lo)/span)*2-1,y=((b->value-c->lo)/span)*2-1;
  if(x!=0&&y!=0){if(c->type==1){float radius=sqrtf(x*x+y*y);if(radius>1){float angle=atan2f(y,x);x=cosf(angle);y=sinf(angle);}}
   else if(c->type==0){float ratio=fabsf(x/y);if(ratio>1)ratio=1/ratio;float scale=1/sqrtf(ratio*ratio+1);x*=scale;y*=scale;
    float radius=sqrtf(x*x+y*y);if(radius>0){float weight=(sinf(radius*3.1415927410125732f*.5f)/radius-1)*(1-cosf(ratio*3.1415927410125732f*.5f))+1;x*=weight;y*=weight;}}
  }
  a->value=((x+1)*.5f)*span+c->lo;b->value=((y+1)*.5f)*span+c->lo;
 }
}
float model_controls_value_in(const EmControlsContext *context,const char *id){
 if(!context)context=current_controls;
 unsigned index=context->range_index[range_slot_in(context,id)];
 if(!index)return timeline_value(id);
 const Range *range=&context->range_data[index-1];
 float coefficient=context->stereo_coefficient;
 if(coefficient!=0&&range->stereo_target&&range->hi!=range->lo){
  float anchor=range->lo+(range->hi-range->lo)*context->stereo_anchor_ratio;
  return (1-coefficient)*range->value+coefficient*anchor;
 }
 return range->value;
}
float model_controls_value(const char *id){return model_controls_value_in(current_controls,id);}
int model_controls_talk_available(void){for(unsigned i=0;i<nm;++i)if(mouths[i].hi>mouths[i].lo)return 1;return 0;}
void model_controls_talk_toggle(void){if(model_controls_talk_available()){talking=!talking;mouth_open=0;}}
int model_controls_talking(void){return talking;}
void model_controls_mouth_open_toggle(void){if(model_controls_talk_available()){mouth_open=!mouth_open;talking=0;}}
int model_controls_mouth_open(void){return mouth_open;}
int model_controls_blink_available(void){for(unsigned i=0;i<ne;++i)if(model_controls_has(eyes[i].id)&&eyes[i].frames>0&&eyes[i].end>eyes[i].begin)return 1;return 0;}
void model_controls_set_blink_enabled(int enabled){
 current_controls->blink_disabled=!enabled;
 if(enabled)return;
 for(unsigned i=0;i<ne;++i){
  Eye *e=eyes+i;
  if(e->state){e->state=0;e->blink=e->begin;e->wait=e->min;}
 }
}
void model_controls_blink_now(void){
 if(current_controls->blink_disabled)return;
 for(unsigned i=0;i<ne;++i){Eye *e=eyes+i;if(e->frames>0&&e->end>e->begin){e->blink=e->begin;e->state=10;}}
}
void model_controls_stop_wind(void){
 windy=0;
 em_wind_init(&wind,-1000,1000);
}
int model_controls_start_wind(float start,float end,float speed,float power_a,float power_b,float player_scale){
 if(!isfinite(start)||!isfinite(end)||!isfinite(speed)||!isfinite(power_a)||!isfinite(power_b))return 0;
 if(speed<0){float from=start;start=end;end=from;speed=-speed;}
 if(start==end||speed==0||(power_a==0&&power_b==0)){model_controls_stop_wind();return 1;}
 if(!isfinite(player_scale)||player_scale==0)return 0;
 float model_start=start/player_scale,model_end=end/player_scale,model_speed=speed/player_scale;
 if(!isfinite(model_start)||!isfinite(model_end)||!isfinite(model_speed))return 0;
 /* The native player retains particles when reconfiguring the same bounds. */
 if(!windy||start!=current_controls->wind_start||end!=current_controls->wind_end){
  em_wind_init(&wind,model_start,model_end);
  current_controls->wind_start=start;
  current_controls->wind_end=end;
 }
 em_wind_configure(&wind,power_a,power_b,model_speed);
 windy=1;
 return 1;
}
void model_controls_wind_toggle(void){
 if(windy)model_controls_stop_wind();
 else model_controls_start_wind(-1000,1000,4,8,2,1);
}
int model_controls_windy(void){return windy;}

int model_controls_has(const char *id){for(unsigned i=0;i<nr;++i)if(!strcmp(ranges[i].id,id))return 1;return 0;}
int model_controls_has_state_variable(const char *id){
 if(!*id)return 0;
 if(current_controls->range_index[range_slot(id)])return 1;
 for(unsigned i=0;i<ne;++i)if(!strcmp(eyes[i].id,id))return 1;
 for(unsigned i=0;i<nm;++i)if(!strcmp(mouths[i].id,id))return 1;
 for(unsigned i=0;i<np;++i){
  const Physics *part=physics+i;
  if(!strcmp(part->lr,id)||!strcmp(part->ud,id)||!strcmp(part->lrm,id))return 1;
 }
 return 0;
}
