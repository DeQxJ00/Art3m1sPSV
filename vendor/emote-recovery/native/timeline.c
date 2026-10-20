/* Binary-derived timeline commands plus a demo lane mixer.
 * 8117A3C0 separates diff timelines; 811810CC reads command records.
 * 81189FFA performs transitions. Lane allocation/UI policy is ours. */
#include "timeline.h"
#include "controls.h"
#include "timeline_commands.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
typedef EmPsbValue V;
static V get(V v,const char *s){V r={0};if(v.data&&*v.data==33)em_psb_find(&v,(const uint8_t*)s,&r);return r;}
static unsigned count(V v){return v.data&&(*v.data==32||*v.data==33)?em_psb_array(v.data+1).count:0;}
static V at(V v,unsigned i){V r={0};if(i<count(v))em_psb_at(&r,&v,i);return r;}
static float num(V v,float d){return v.data&&*v.data!=1?em_psb_float(&v):d;}
static float prop(V v,const char *k,float d){return num(get(v,k),d);}
static const char *str(V v){return v.data&&*v.data>=21&&*v.data<=24?em_psb_string(&v):"";}
typedef EmTimelineCommandKey Key;
typedef struct VariableQueue VariableQueue;
typedef struct {const char *name;VariableQueue *command;Key *keys;unsigned count;size_t cursor;float value,initial,start,target,duration,elapsed,exponent;int active,instant,selected_key,overridden,override_key;} Track;
typedef struct {Track tracks[128];int count,index,diff,playing;unsigned flags;unsigned long long order;float time,length,begin,end;const char *label;VariableQueue *weight;int remove_after_fade,loop_entered,command_playback;float weight_value;} Lane;
/* sample01 e-mote.lua: face/action use PlayTimeline(...,1), idle uses
 * FadeInTimeline. DLL 18001B800 tests flag 2, not metadata.diff, for addition.
 * Keep written normal parameters after their timeline completes. */
struct VariableQueue {EmTransition control;EmTransitionCommand *commands;float value,start,target;};
typedef struct {const char *name;float value,start,target,duration,elapsed,exponent;int settling,timeline_pending;char *owned_name;VariableQueue *queue;} Base;
struct EmTimelineContext {
 unsigned long long next_order_data;Lane *lane_data;int lane_count_data,lane_capacity_data,ui_slot_data[3];int focus_data,index_data,count_data;V timeline_data,instant_data;
 int *category_data;Base base_data[512];unsigned base_count_data;int script_data;
 uint16_t base_index[1024]; /* Base index + 1; zero means empty. */
 uint64_t idle_seed;
};
static EmTimelineContext default_timeline;
static __thread EmTimelineContext *current_timeline=&default_timeline;
#define lanes (current_timeline->lane_data)
#define lane_count (current_timeline->lane_count_data)
#define lane_capacity (current_timeline->lane_capacity_data)
#define ui_slots (current_timeline->ui_slot_data)
#define focus (current_timeline->focus_data)
#define index_ (current_timeline->index_data)
#define count_ (current_timeline->count_data)
#define timelines (current_timeline->timeline_data)
#define instant_variables (current_timeline->instant_data)
#define categories (current_timeline->category_data)
#define bases (current_timeline->base_data)
#define base_count (current_timeline->base_count_data)
#define script_playback (current_timeline->script_data)
EmTimelineContext *timeline_context_create(void){
 EmTimelineContext *context=calloc(1,sizeof(*context));
 if(context){context->focus_data=-1;for(int i=0;i<3;++i)context->ui_slot_data[i]=-1;}
 return context;
}
EmTimelineContext *timeline_context_use(EmTimelineContext *context){
 EmTimelineContext *previous=current_timeline;current_timeline=context?context:&default_timeline;return previous;
}
/* Index names only: transitions and implicit publication keep live values.
 * Store indices because SetVariable may replace the owned name pointer. */
static unsigned base_slot(const char *name){unsigned hash=2166136261u;for(const unsigned char*s=(const unsigned char*)name;*s;++s)hash=(hash^*s)*16777619u;
 unsigned slot=hash&1023u;while(current_timeline->base_index[slot]&&strcmp(bases[current_timeline->base_index[slot]-1].name,name))slot=(slot+1)&1023u;return slot;}
static Base *base_find(const char *name){unsigned index=current_timeline->base_index[base_slot(name)];return index?bases+index-1:NULL;}
static float base_value(const char *name){Base*b=base_find(name);return b?b->value:0;}
static Base *base_entry(const char *name){unsigned slot=base_slot(name),index=current_timeline->base_index[slot];if(index)return bases+index-1;if(base_count==512)return NULL;unsigned j=base_count++;bases[j]=(Base){.name=name};current_timeline->base_index[slot]=(uint16_t)(j+1);return bases+j;}
/* 8118A22A, allocation-independent command semantics. Queue capacity and
 * command blocks are owned by this adapter; original deque allocation is not
 * reproduced. A non-positive duration ignores append and clears all work. */
static void variable_command(EmTransition *c,const float *target,int append,float duration,float exponent){
 if(!(duration>0)){
  if(c->queued)c->head=0;c->queued=0;c->state=0;
  for(unsigned i=0;i<c->dimensions;++i)c->current[i]=target[i];
  return;
 }
 /* Win 18002C4B0: keep an equal active target that finishes sooner. */
 if(script_playback&&!append&&c->state&&c->inverse_duration>0&&
    !memcmp(c->target,target,c->dimensions*sizeof(float))&&duration>(1-c->progress)/c->inverse_duration)return;
 if(!append){if(c->queued)c->head=0;c->queued=0;c->state=0;}
 c->head&=c->capacity-1;
 EmTransitionCommand *command=c->queue[(c->head+c->queued)&(c->capacity-1)];
 for(unsigned i=0;i<c->dimensions;++i)command->target[i]=target[i];
 command->duration=duration;command->exponent=exponent;++c->queued;
}
/* 8118A198: pending tail wins over an in-progress target. */
static void variable_finish(EmTransition *c){
 if(c->queued){
  const float *target=c->queue[(c->head+c->queued-1)&(c->capacity-1)]->target;
  for(unsigned i=0;i<c->dimensions;++i)c->current[i]=target[i];
  c->queued=c->head=0;c->state=0;
 }else if(c->state){
  for(unsigned i=0;i<c->dimensions;++i)c->current[i]=c->target[i];
  c->state=0;
 }
}
static void base_clear(EmTimelineContext *context){
 for(unsigned i=0;i<context->base_count_data;++i){Base *b=context->base_data+i;
  if(b->queue){free(b->queue->control.queue);free(b->queue->commands);free(b->queue);}
  free(b->owned_name);
 }
 context->base_count_data=0;memset(context->base_index,0,sizeof context->base_index);
}
static int variable_reserve(VariableQueue *q,unsigned count){
 EmTransition *c=&q->control;if(count<c->capacity)return 1;
 unsigned capacity=c->capacity?c->capacity:8;
 while(capacity<=count){if(capacity>0x10000000u)return 0;capacity*=2;}
 EmTransitionCommand *commands=calloc(capacity,sizeof(*commands));
 EmTransitionCommand **queue=calloc(capacity,sizeof(*queue));
 if(!commands||!queue){free(commands);free(queue);return 0;}
 for(unsigned i=0;i<capacity;++i)queue[i]=commands+i;
 for(unsigned i=0;i<c->queued;++i)commands[i]=*c->queue[(c->head+i)&(c->capacity-1)];
 free(c->queue);free(q->commands);q->commands=commands;c->queue=queue;c->capacity=capacity;c->head=0;return 1;
}
static int track_route(unsigned flags,int instant);
int timeline_set_variable(const char *name,float value,float duration,float easing,int append){
 if(!name||!*name||!isfinite(value)||!isfinite(duration)||!isfinite(easing))return 0;
 size_t n=strlen(name)+1;char *copy=malloc(n);if(!copy)return 0;memcpy(copy,name,n);
 Base *b=base_entry(copy);if(!b){free(copy);return 0;}
 if(!b->owned_name){b->owned_name=copy;b->name=copy;}else free(copy);
 if(!b->queue){b->queue=calloc(1,sizeof(*b->queue));if(!b->queue)return 0;
  EmTransition *c=&b->queue->control;c->dimensions=1;c->current=&b->queue->value;c->start=&b->queue->start;c->target=&b->queue->target;
 }
 EmTransition *c=&b->queue->control;
 if(duration>0&&!variable_reserve(b->queue,append?c->queued+1:1))return 0;
 if(!em_transition_active(c))b->queue->value=b->value;
 variable_command(c,&value,append,duration,easing>0?easing+1:1/(1-easing));
 b->settling=b->timeline_pending=0;b->value=b->queue->value;
 /* External writes replace the live normal controller. An already consumed
  * key must not be reissued by analytic sampling on every subsequent Tick.
  * A later enabled key regains ownership; additive lanes remain independent. */
 if(script_playback)for(int i=0;i<lane_count;++i){Lane*l=lanes+i;if(!l->playing)continue;
  for(int j=0;j<l->count;++j){Track*t=l->tracks+j;
   if(track_route(l->flags,t->instant)==1&&!strcmp(t->name,name)){t->overridden=1;t->override_key=t->selected_key;}
  }
 }
 return 1;
}
int timeline_finish_variable(const char *name){
 if(!name)return 0;for(unsigned i=0;i<base_count;++i){Base *b=bases+i;if(strcmp(b->name,name))continue;
  if(b->queue){variable_finish(&b->queue->control);b->value=b->queue->value;}return 1;
 }return 0;
}
/* Win 18002C580 differs from PSV 81189FFA: a dequeued transition consumes
 * this delta immediately, and completion drains further commands with the same
 * delta. The host also uses a 0.0001 completion tolerance. */
static void host_transition_step(EmTransition *c,float *out,float frames){
 for(;;){
  if(!c->state&&c->queued){EmTransitionCommand command=*c->queue[c->head&(c->capacity-1)];
   --c->queued;c->head=c->queued?c->head+1:0;
   for(unsigned i=0;i<c->dimensions;++i){c->start[i]=c->current[i];c->target[i]=command.target[i];}
   c->progress=0;c->exponent=command.exponent;c->inverse_duration=1/command.duration;c->state=1;
  }
  if(c->state!=1)break;
  float progress=c->progress+frames*c->inverse_duration;c->progress=progress;
  if(progress-1>=-.0001f){c->progress=1;for(unsigned i=0;i<c->dimensions;++i)c->current[i]=c->target[i];c->state=0;continue;}
  float weight=powf(progress,c->exponent);
  for(unsigned i=0;i<c->dimensions;++i)c->current[i]=c->start[i]+(c->target[i]-c->start[i])*weight;
  break;
 }
 for(unsigned i=0;i<c->dimensions;++i)out[i]=c->current[i];
}
static void variable_update(EmTransition*c,float*out,float frames){if(script_playback)host_transition_step(c,out,frames);else em_transition_step(c,out,frames);}
static void variable_step(float frames){
 for(unsigned i=0;i<base_count;++i){Base *b=bases+i;if(b->queue&&em_transition_active(&b->queue->control))variable_update(&b->queue->control,&b->value,frames);}
}
/* sample01 18001E24B..18001E26B and 18001F160: instantVariableList
 * channels remain absolute in a diff timeline; internal flag 4 retires them. */
static int track_route(unsigned flags,int instant){
 if((flags&4u)&&instant)return 0;
 return (flags&2u)&&!instant?2:1;
}
/* Demo arbitration: a normal lane owns its written channels and cancels
 * pending external variable work. This extends the existing settling policy;
 * only command/controller semantics, not this priority rule, have ARM parity. */
static void commit(Lane *l){for(int i=0;i<l->count;++i){Track *t=l->tracks+i;if(!t->active||track_route(l->flags,t->instant)!=1)continue;if(t->overridden&&t->selected_key==t->override_key)continue;t->overridden=0;Base *b=base_entry(t->name);if(b){b->value=t->value;b->settling=0;b->timeline_pending=t->duration>t->elapsed;b->start=t->start;b->target=t->target;b->duration=t->duration;b->elapsed=t->elapsed;b->exponent=t->exponent;if(b->queue){b->queue->control.state=0;b->queue->control.queued=b->queue->control.head=0;b->queue->value=b->value;}}}}
/* PSV normal commands are submitted on key crossings, not recommitted each
 * render frame. Windows keeps its separate adapter until its caller is recovered. */
static int normal_sink(void *track,float value,float duration,float easing){Track*t=track;return timeline_set_variable(t->name,value,duration,easing,0);}
static void normal_seek_commands(Lane*l){
 for(int j=0;j<l->count;++j){Track*t=l->tracks+j;if(track_route(l->flags,t->instant)!=1)continue;
  t->cursor=0;while(t->cursor+1<t->count&&t->keys[t->cursor+1].time<=l->time)++t->cursor;
  /* UI seek keeps its analytic current pose, then resumes the remaining command. */
  Key*k=t->keys+t->cursor;if(k->disabled)continue;
  float duration=t->cursor+1<t->count?t->keys[t->cursor+1].time-l->time-1:0;
  normal_sink(t,k->value,duration>0?duration:0,k->easing);
 }
}
static void normal_forward_commands(Lane*l,float time,int inclusive){
 for(int j=0;j<l->count;++j){Track*t=l->tracks+j;if(track_route(l->flags,t->instant)!=1)continue;
  if(t->count>=2&&t->keys[t->count-1].disabled)em_timeline_commands_forward_inline(t->keys,t->count,&t->cursor,time,inclusive,normal_sink,t);
  else{ /* Explicit adapter extension for host fixtures without terminal sentinel. */
   while(t->cursor+1<t->count&&(inclusive?t->keys[t->cursor+1].time<=time:t->keys[t->cursor+1].time<time)){
    Key*k=t->keys+t->cursor;if(!k->disabled)normal_sink(t,k->value,0,0);k=t->keys+(++t->cursor);
    if(!k->disabled){float duration=t->cursor+1<t->count?t->keys[t->cursor+1].time-time-1:0;normal_sink(t,k->value,duration>0?duration:0,k->easing);}
   }
  }
 }
}
static void settle_step(float frames){for(unsigned i=0;i<base_count;++i){Base *b=bases+i;if(!b->settling)continue;b->elapsed+=frames;float current=b->start;EmTransition c={.dimensions=1,.state=1,.current=&current,.start=&b->start,.target=&b->target,.exponent=b->exponent,.inverse_duration=1/b->duration};em_transition_step(&c,&b->value,b->elapsed);if(b->elapsed>=b->duration){b->value=b->target;b->settling=0;}}}
static void free_queue(VariableQueue *q){if(q){free(q->control.queue);free(q->commands);free(q);}}
static void clear_lane(Lane *l){if(l->weight){free(l->weight->control.queue);free(l->weight->commands);free(l->weight);}for(int i=0;i<l->count;++i){free(l->tracks[i].keys);free_queue(l->tracks[i].command);}memset(l,0,sizeof(*l));l->index=-1;l->begin=l->end=-1;l->label="";}
void timeline_context_destroy(EmTimelineContext *context){
 if(!context||context==&default_timeline)return;
 if(current_timeline==context)current_timeline=&default_timeline;
 for(int i=0;i<context->lane_count_data;++i)clear_lane(context->lane_data+i);free(context->lane_data);
 base_clear(context);free(context->category_data);free(context);
}
static void evaluate(Lane *l,float frame){
 if(l->command_playback){for(int j=0;j<l->count;++j){free_queue(l->tracks[j].command);l->tracks[j].command=NULL;}l->command_playback=0;}
 l->time=fmaxf(0,fminf(frame,l->length));
 for(int i=0;i<l->count;++i){Track *tr=l->tracks+i;if(!track_route(l->flags,tr->instant))continue;float previous=tr->initial;tr->value=previous;tr->active=0;tr->selected_key=-1;
  for(unsigned j=0;j<tr->count;++j){Key k=tr->keys[j];if(k.time>l->time)break;if(k.disabled)continue;
   tr->active=1;tr->selected_key=(int)j;float duration=j+1<tr->count?tr->keys[j+1].time-k.time-1:0;
   float elapsed=l->time-k.time;
   tr->start=previous;tr->target=k.value;tr->duration=duration;tr->elapsed=elapsed;tr->exponent=k.easing>0?k.easing+1:1/(1-k.easing);
   if(duration<=0)tr->value=k.value;
   else {
    /* Restore transition state analytically for deterministic seeking, then
     * execute the recovered arithmetic (dequeue does not consume time). */
    float start=previous,target=k.value,current=previous;
    EmTransition c={.dimensions=1,.state=1,.current=&current,.start=&start,.target=&target,
      .exponent=k.easing>0?k.easing+1:1/(1-k.easing),.inverse_duration=1/duration};
    em_transition_step(&c,&tr->value,elapsed);
   }
   if(j+1<tr->count&&l->time<tr->keys[j+1].time)break;
   previous=k.value;tr->value=previous;
  }
 }
}
/* Step/Skip uses the recovered Win boundary command producer, not a large
 * time delta or an analytic seek. Keep command state for subsequent Tick calls
 * on a loop entered by Step. Other preview/normal playback policy is unchanged. */
static int boundary_command(Lane *l,Track *t,unsigned index,float time){
 Key *k=t->keys+index;if(k->disabled)return 1;
 float duration=index+1<t->count?fmaxf(t->keys[index+1].time-time-1,0):0;
 int route=track_route(l->flags,t->instant);if(!route)return 1;
 if(route==1)return timeline_set_variable(t->name,k->value,duration,k->easing,0);
 if(!t->command)return 1; /* Retired diff tracks are never sampled again. */
 if(duration>0&&!variable_reserve(t->command,1))return 0;
 variable_command(&t->command->control,&k->value,0,duration,k->easing>0?k->easing+1:1/(1-k->easing));t->active=1;return 1;
}
static int boundary_forward(Lane *l,float time,int inclusive){
 for(int j=0;j<l->count;++j){Track *t=l->tracks+j;if(!track_route(l->flags,t->instant))continue;
  while(t->cursor+1<t->count&&(inclusive?t->keys[t->cursor+1].time<=time:t->keys[t->cursor+1].time<time)){
   ++t->cursor;if(!boundary_command(l,t,(unsigned)t->cursor,time))return 0;
  }
 }l->time=time;return 1;
}
static int boundary_seek(Lane *l,float time){
 for(int j=0;j<l->count;++j){Track *t=l->tracks+j;if(!track_route(l->flags,t->instant))continue;
  unsigned cursor=0;int last=-1;
  for(;cursor+1<t->count;++cursor){if(!t->keys[cursor].disabled)last=(int)cursor;
   if(time>=t->keys[cursor].time&&t->keys[cursor+1].time>time)break;
  }
  t->cursor=cursor;if(last>=0&&!boundary_command(l,t,(unsigned)last,time))return 0;
 }l->time=time;return 1;
}
static int boundary_prepare(Lane *l){
 if(l->command_playback)return 1;
 for(int j=0;j<l->count;++j){Track *t=l->tracks+j;t->cursor=0;
  while(t->cursor+1<t->count&&t->keys[t->cursor+1].time<=l->time)++t->cursor;
  if(track_route(l->flags,t->instant)!=2||t->command)continue;
  VariableQueue *q=calloc(1,sizeof(*q));if(!q)return 0;t->command=q;q->value=t->value;
  q->control=(EmTransition){.dimensions=1,.current=&q->value,.start=&q->start,.target=&q->target};
  /* Import the adapter's live transition once, without snapping its value. */
  if(t->active&&t->duration>t->elapsed){q->start=t->start;q->target=t->target;q->control.state=1;
   q->control.progress=t->elapsed/t->duration;q->control.inverse_duration=1/t->duration;q->control.exponent=t->exponent;}
  if(!variable_reserve(q,1))return 0;
 }l->command_playback=1;return 1;
}
static void boundary_advance_diff(Lane *l,float frames){
 if(!l->diff)return;
 if(l->weight)variable_update(&l->weight->control,&l->weight_value,frames);
 for(int j=0;j<l->count;++j){Track*t=l->tracks+j;if(t->command&&track_route(l->flags,t->instant)==2)host_transition_step(&t->command->control,&t->value,frames);}
}
int timeline_finish_playback(int skip){
 /* Import analytic normal controllers once; explicit SetVariable has already
  * cleared this marker, so newer script commands remain authoritative. */
 for(unsigned i=0;i<base_count;++i){Base*b=bases+i;if(!b->timeline_pending)continue;
  float value=b->value,start=b->start,target=b->target,duration=b->duration,elapsed=b->elapsed,exponent=b->exponent;
  if(!timeline_set_variable(b->name,target,duration,0,0))return 0;
  VariableQueue*q=b->queue;q->value=value;q->start=start;q->target=target;
  q->control.queued=q->control.head=0;q->control.state=1;q->control.progress=elapsed/duration;q->control.inverse_duration=1/duration;q->control.exponent=exponent;
 }
 /* Active vector order, including restarts and reused slots (18001E860/AB0). */
 unsigned long long previous=0;
 for(int n=0;n<lane_count;++n){Lane*l=NULL;for(int j=0;j<lane_count;++j){Lane*c=lanes+j;
   if(c->index>=0&&c->playing&&c->order>previous&&(!l||c->order<l->order))l=c;}
  if(!l)break;previous=l->order;
  if(l->begin>=0&&(skip||!l->remove_after_fade)){
   if(!boundary_prepare(l))return 0;
   if(!skip&&!l->loop_entered){float remaining=l->end-l->time;
    if(!boundary_forward(l,l->end,0)||!boundary_seek(l,l->begin)||!boundary_forward(l,l->begin,1))return 0;
    boundary_advance_diff(l,remaining);l->loop_entered=1;
   }
   /* Finish changes controller current; published diff weight updates on Tick. */
   if(l->weight)variable_finish(&l->weight->control);
  }else{
   if(!l->command_playback)for(int j=0;j<l->count;++j){Track*t=l->tracks+j;t->cursor=0;while(t->cursor+1<t->count&&t->keys[t->cursor+1].time<=l->time)++t->cursor;}
   if(!boundary_forward(l,l->length,1))return 0;l->playing=0;
  }
 }return 1;
}
void timeline_finish_variables(void){
 for(unsigned i=0;i<base_count;++i){Base*b=bases+i;
  if(b->settling){b->value=b->target;b->settling=0;}
  b->timeline_pending=0;
  if(b->queue&&em_transition_active(&b->queue->control)){variable_finish(&b->queue->control);b->value=b->queue->value;}
 }
}
/* sample01 18001EEF8..18001EF33: looped timelines survive Pass;
 * a nonlooping differential timeline starts only one 20-frame fade to zero.
 * Bit 4 is an internal "Pass already issued" state, not a caller play flag. */
static int pass_action(unsigned flags,float loop_begin){
 if(loop_begin>=0)return 0;
 if(flags&2u)return (flags&4u)?0:2;
 return 1;
}
static void pass_normal(void){
 /* 18001ED70 reissues remaining normal keys through SetVariable using each
  * key's time as duration, then removes that timeline. It does not snap all
  * variables immediately to the final pose; replacement actions override
  * only their own channels. Looped differential idle is left alone. */
 for(int i=0;i<lane_count;++i){Lane *l=lanes+i;if(l->index<0||!l->playing)continue;
  int action=pass_action(l->flags,l->begin);if(!action)continue;
  if(action==2){if(!timeline_fade(l->label,0,20,0,1))continue;l->flags|=4u;}
  if(l->time>=l->length)continue;
  for(int j=0;j<l->count;++j){Track *t=l->tracks+j;if(action==2&&!t->instant)continue;Base *b=base_entry(t->name);if(!b)continue;
   if(t->active&&t->duration>t->elapsed){b->start=t->start;b->target=t->target;b->duration=t->duration;b->elapsed=t->elapsed;b->exponent=t->exponent;b->settling=1;}
   for(unsigned k=0;k<t->count;++k){Key key=t->keys[k];if(key.disabled||key.time<=l->time)continue;
    if(b->settling&&b->target==key.value&&key.time>b->duration-b->elapsed)continue;
    b->start=b->value;b->target=key.value;b->duration=key.time;b->elapsed=0;b->exponent=key.easing>0?key.easing+1:1/(1-key.easing);b->settling=key.time>0;
    if(!b->settling)b->value=key.value;
   }
  }if(action==1){l->time=l->length;l->playing=0;}
 }
}
static void load_lane(int slot,int index,unsigned flags){
 Lane *l=lanes+slot;clear_lane(l);l->index=index;V t=at(timelines,index);l->label=str(get(t,"label"));l->flags=flags;l->diff=(flags&2)!=0;l->playing=1;l->weight_value=1;
 l->begin=prop(t,"loopBegin",-1);l->end=prop(t,"loopEnd",-1);l->length=prop(t,"lastTime",-1);float last=0;
 V list=get(t,"variableList");
 for(unsigned i=0;i<count(list)&&l->count<128;++i){V item=at(list,i),frames=get(item,"frameList");unsigned n=count(frames);if(!n)continue;
  Track *tr=l->tracks+l->count;*tr=(Track){.name=str(get(item,"label")),.count=n,.keys=calloc(n,sizeof(Key))};if(!tr->keys)continue;++l->count;
  for(unsigned k=0;k<count(instant_variables);++k)if(!strcmp(str(at(instant_variables,k)),tr->name)){tr->instant=1;break;}
  tr->initial=(!l->diff||tr->instant)?base_value(tr->name):0;
  for(unsigned j=0;j<n;++j){V f=at(frames,j),c=get(f,"content");tr->keys[j]=(Key){.time=prop(f,"time",0),.value=prop(c,"value",0),.easing=prop(c,"easing",0),.disabled=prop(f,"type",0)==0};last=fmaxf(last,tr->keys[j].time);}
 }
 if(l->length<0)l->length=last;evaluate(l,0);commit(l);if(!script_playback)normal_seek_commands(l);
}
static void timeline_bind_internal(V root,int autoplay,int classify){
 for(int i=0;i<lane_count;++i)clear_lane(lanes+i);free(lanes);lanes=NULL;lane_count=lane_capacity=0;for(int i=0;i<3;++i)ui_slots[i]=-1;free(categories);categories=NULL;
 base_clear(current_timeline);script_playback=!strcmp(str(get(root,"spec")),"win");
 instant_variables=script_playback?get(get(root,"metadata"),"instantVariableList"):(V){0};
 timelines=get(get(root,"metadata"),"timelineControl");count_=count(timelines);index_=0;focus=-1;
 if(!classify)return; /* Script labels never consult demo categories. */
 categories=calloc(count_?count_:1,sizeof(int));
 for(int i=0;i<count_;++i){V t=at(timelines,i);float last=0;V vars=get(t,"variableList");unsigned keys=0;
  for(unsigned j=0;j<count(vars);++j){V fs=get(at(vars,j),"frameList");for(unsigned k=0;k<count(fs);++k){V f=at(fs,k);last=fmaxf(last,prop(f,"time",0));if(prop(f,"type",0)!=0)++keys;}}
  categories[i]=!keys?-1:prop(t,"diff",0)==0&&last<=1?0:prop(t,"loopBegin",-1)>=0&&prop(t,"loopEnd",-1)>prop(t,"loopBegin",-1)?1:2;
 }
 if(autoplay&&count_)timeline_select(0);
}
void timeline_bind(V root){timeline_bind_internal(root,1,1);}
void timeline_bind_neutral(V root){timeline_bind_internal(root,0,1);}
void timeline_bind_script(V root){timeline_bind_internal(root,0,0);}
void timeline_seed_idle(uint64_t seed){current_timeline->idle_seed=seed;}
static Lane empty_lane={.index=-1,.begin=-1,.end=-1,.label=""};
static Lane *focused_lane(void){return focus>=0&&focus<lane_count?lanes+focus:&empty_lane;}
/* Original Win DLL 18001FDD0 normalizes U+FF01..U+FF5E before lookup.
 * Keep this scoped to Win models; PSV name comparisons remain byte-exact. */
static unsigned timeline_name_byte(const unsigned char **cursor){
 const unsigned char *p=*cursor;unsigned value=*p;
 if(p[0]==0xef&&((p[1]==0xbc&&p[2]>=0x81&&p[2]<=0xbf)||(p[1]==0xbd&&p[2]>=0x80&&p[2]<=0x9e))){
  value=p[1]==0xbc?p[2]-0x60:p[2]-0x20;*cursor=p+3;
 }else if(value)*cursor=p+1;
 return value;
}
static int timeline_name_equal(const char *a,const char *b){
 if(!script_playback)return !strcmp(a,b);
 const unsigned char *p=(const unsigned char*)a,*q=(const unsigned char*)b;
 for(;;){unsigned x=timeline_name_byte(&p),y=timeline_name_byte(&q);if(x!=y)return 0;if(!x)return 1;}
}
static int playing_slot(const char *label){if(!label)return -1;for(int i=0;i<lane_count;++i)if(lanes[i].index>=0&&timeline_name_equal(lanes[i].label,label))return i;return -1;}
int timeline_is_playing(const char *label){if(!label)return 0;if(!*label)return timeline_active_count()>0;int i=playing_slot(label);return i>=0&&lanes[i].playing;}
int timeline_active_count(void){int n=0;for(int i=0;i<lane_count;++i)n+=lanes[i].index>=0&&lanes[i].playing;return n;}
int timeline_stop(const char *label){
 int stopped=0;if(!label)return 0;
 for(int i=0;i<lane_count;++i){Lane *l=lanes+i;if(l->index<0||(*label&&!timeline_name_equal(l->label,label)))continue;
  clear_lane(l);for(int c=0;c<3;++c)if(ui_slots[c]==i)ui_slots[c]=-1;if(focus==i)focus=-1;++stopped;
 }return stopped;
}
int timeline_has_label(const char *label){
 if(!label||!*label)return 0;
 for(int i=0;i<count_;++i)if(timeline_name_equal(str(get(at(timelines,i),"label")),label))return 1;
 return 0;
}
int timeline_play(const char *label,unsigned flags){
 if(!label||!*label||(flags&~3u))return 0;int index=-1;
 for(int i=0;i<count_;++i)if(timeline_name_equal(str(get(at(timelines,i),"label")),label)){index=i;break;}
 if(index<0)return 0;
 /* 18001B800: bit 0 preserves concurrent timelines; bit 1 selects additive
  * tracks. Playing the same name restarts its unique active record. */
 if(!(flags&1))timeline_stop("");
 int slot=playing_slot(label);
 if(slot<0){
  for(int i=0;i<lane_count;++i)if(lanes[i].index<0){slot=i;break;}
  if(slot<0){if(lane_count==lane_capacity){int capacity=lane_capacity?lane_capacity*2:4;Lane *p=realloc(lanes,(size_t)capacity*sizeof(*p));if(!p)return 0;lanes=p;lane_capacity=capacity;}
   slot=lane_count++;memset(lanes+slot,0,sizeof(*lanes));}
 }
 unsigned long long order=lanes[slot].playing?lanes[slot].order:++current_timeline->next_order_data;
 load_lane(slot,index,flags);lanes[slot].order=order;focus=slot;index_=index;return 1;
}
void timeline_pass(void){pass_normal();}
/* PSV 81180BD2 queues the current normal key from the live shared value;
 * disabled intervals leave external commands untouched. Keep analytic preview
 * for UI seek, and the separately scoped Windows/diff adapter behavior. */
int timeline_seek_named(const char *label,float frame){int i=playing_slot(label);if(i<0||!isfinite(frame))return 0;Lane*l=lanes+i;for(int j=0;j<l->count;++j)l->tracks[j].overridden=0;evaluate(l,frame);l->playing=1;if(script_playback||l->diff)commit(l);if(!script_playback)normal_seek_commands(l);return 1;}
int timeline_fade(const char *label,float target,float duration,float easing,int remove_after){
 int i=playing_slot(label);if(i<0||!isfinite(target)||!isfinite(duration)||!isfinite(easing))return 0;Lane *l=lanes+i;
 if(!l->weight){l->weight=calloc(1,sizeof(*l->weight));if(!l->weight)return 0;VariableQueue *q=l->weight;q->value=1;q->control=(EmTransition){.dimensions=1,.current=&q->value,.start=&q->start,.target=&q->target};}
 if(duration>0&&!variable_reserve(l->weight,1))return 0;
 variable_command(&l->weight->control,&target,0,duration,easing>0?easing+1:1/(1-easing));l->remove_after_fade=remove_after;return 1;
}
int timeline_fade_in(const char *label,float duration,float easing){
 if(!timeline_is_playing(label)){
  if(!timeline_play(label,3))return 0;
  Lane *l=lanes+playing_slot(label);
  /* Adapter presentation policy, not a recovered random-start ABI. Restrict
   * it to Win additive loops with no intro or enabled instant tracks, so
   * expressions and one-shot/explicit playback keep their authored timing. */
  int eligible=script_playback&&current_timeline->idle_seed&&l->diff&&l->begin==0&&l->end>0&&isfinite(l->end);
  for(int j=0;eligible&&j<l->count;++j)if(l->tracks[j].instant)
   for(unsigned k=0;k<l->tracks[j].count;++k)if(!l->tracks[j].keys[k].disabled){eligible=0;break;}
  if(eligible){
   uint64_t z=current_timeline->idle_seed;
   for(const unsigned char *s=(const unsigned char*)l->label;*s;++s)z=(z^*s)*UINT64_C(1099511628211);
   z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);z^=z>>31;
   float phase=(z>>40)*(1.f/16777216.f);
   evaluate(l,l->end*phase);l->loop_entered=1;
  }
  if(!timeline_fade(label,0,0,0,0))return 0;
 }
 return timeline_fade(label,1,duration,easing,0);
}
void timeline_select(int index){
 if(!count_||!categories)return;index=(index%count_+count_)%count_;int c=categories[index];if(c<0)return;
 if(script_playback&&c!=1)timeline_pass();
 if(ui_slots[c]>=0&&ui_slots[c]<lane_count){const char *old=lanes[ui_slots[c]].label;timeline_stop(old);}
 const char *label=str(get(at(timelines,index),"label"));
 int ok=script_playback&&c==1?timeline_fade_in(label,0,0):timeline_play(label,1u|((script_playback?0:prop(at(timelines,index),"diff",0)!=0)?2u:0u));
 if(ok){focus=playing_slot(label);index_=index;ui_slots[c]=focus;}
}
void timeline_seek(float frame){Lane *l=focused_lane();if(l==&empty_lane)return;for(int j=0;j<l->count;++j)l->tracks[j].overridden=0;evaluate(l,frame);l->playing=1;commit(l);if(!script_playback)normal_seek_commands(l);}
/* PCSG00341 active-name tree compares raw unsigned bytes, then length.
 * sample01 uses its own active-name vector; do not apply this ordering there. */
static int name_order(const char*a,const char*b){
 const unsigned char*x=(const unsigned char*)a,*y=(const unsigned char*)b;
 while(*x&&*x==*y){++x;++y;}return (int)*x-(int)*y;
}
void timeline_step(float frames){
 if(script_playback)variable_step(frames);settle_step(frames);
 const char*previous_name=NULL;unsigned long long previous_order=0;
 for(int n=0;n<lane_count;++n){Lane*l=NULL;
  if(script_playback){for(int i=0;i<lane_count;++i){Lane*c=lanes+i;if(c->index>=0&&c->playing&&c->order>previous_order&&(!l||c->order<l->order))l=c;}
   if(!l)break;previous_order=l->order;
  }
  else{for(int i=0;i<lane_count;++i){Lane*c=lanes+i;if(c->index>=0&&c->playing&&(!previous_name||name_order(c->label,previous_name)>0)&&(!l||name_order(c->label,l->label)<0))l=c;}
   if(!l)break;previous_name=l->label;
  }
  if(l->index<0||!l->playing)continue;
  if(l->command_playback&&frames>=0){
   if(frames==0)continue;
   boundary_advance_diff(l,frames);
   float remaining=frames;
   if(l->begin>=0&&l->end>l->begin)while(l->time+remaining>=l->end){
    float consumed=l->end-l->time;remaining-=consumed;
    if(!boundary_forward(l,l->end,0)||!boundary_seek(l,l->begin))return;l->loop_entered=1;
   }
   if(!boundary_forward(l,l->time+fmaxf(remaining,0),1))return;
   if((l->begin<0&&l->time>=l->length)||(l->remove_after_fade&&l->weight&&!em_transition_active(&l->weight->control)))l->playing=0;
   continue;
  }
  if(l->weight){variable_update(&l->weight->control,&l->weight_value,frames);if(l->remove_after_fade&&!em_transition_active(&l->weight->control)){l->playing=0;continue;}}
  float next=l->time+frames;if(l->begin>=0&&l->end>l->begin&&next>=l->end){l->loop_entered=1;next=l->begin+fmodf(next-l->begin,l->end-l->begin);}
  if(script_playback&&!l->diff&&l->time>=l->length&&next>=l->length){l->playing=0;continue;}
  if(!script_playback){
   if(frames<0){evaluate(l,next);commit(l);normal_seek_commands(l);} /* UI reverse sampling remains analytic. */
   else{
    float remaining=frames,position=l->time;
    if(l->begin>=0&&l->end>l->begin)while(position+remaining>=l->end){
     float consumed=l->end-position;normal_forward_commands(l,l->end,0);
     l->time=l->begin;normal_seek_commands(l);position=l->begin;
     float rest=remaining-consumed;if(rest==remaining&&consumed>0)break;remaining=rest;
    }
    normal_forward_commands(l,position+remaining,1);
   }
  }
  evaluate(l,next);
 }
 /* Apply written normal parameters in play-call order, independent of freed
  * slot reuse and of UI category. PSV commands already ran in name-tree order;
  * this pass only retires completed PSV lanes. */
 unsigned long long previous=0;
 for(int n=0;n<lane_count;++n){Lane *next=NULL;for(int i=0;i<lane_count;++i){Lane *l=lanes+i;if(l->index>=0&&l->playing&&l->order>previous&&(!next||l->order<next->order))next=l;}if(!next)break;if(script_playback&&!next->command_playback)commit(next);previous=next->order;
  if(next->begin<0&&next->time>=next->length)next->playing=0;
 }
 if(!script_playback)variable_step(frames);
}
int timeline_publish_control(const char *name,float value){
 if(!name||!*name||!isfinite(value))return 0;
 for(unsigned i=0;i<base_count;++i)if(!strcmp(bases[i].name,name)){bases[i].value=value;return 1;}
 size_t n=strlen(name)+1;char *copy=malloc(n);if(!copy)return 0;memcpy(copy,name,n);
 Base *b=base_entry(copy);if(!b){free(copy);return 0;}b->owned_name=copy;b->value=value;return 1;
}
float timeline_value(const char *name){float value=base_value(name);
 /* 18001F160 accumulates base + each weighted diff in active-list order. */
 unsigned long long previous=0;
 for(int n=0;n<lane_count;++n){Lane *next=NULL;for(int j=0;j<lane_count;++j){Lane *l=lanes+j;if(l->index>=0&&l->playing&&l->diff&&l->order>previous&&(!next||l->order<next->order))next=l;}if(!next)break;previous=next->order;
  for(int i=0;i<next->count;++i)if(!strcmp(next->tracks[i].name,name)){if(track_route(next->flags,next->tracks[i].instant)==2&&(!script_playback||next->tracks[i].active))value+=next->tracks[i].value*(next->weight?next->weight_value:1);break;}}
 return value;
}
float timeline_time(void){return focused_lane()->time;}float timeline_length(void){return focused_lane()->length;}
int timeline_index(void){return index_;}int timeline_count(void){return count_;}
int timeline_looping(void){Lane *l=focused_lane();return l->begin>=0&&l->end>l->begin;}
int timeline_finished(void){return !timeline_looping()&&timeline_time()>=timeline_length();}
const char *timeline_label(void){return focused_lane()->label;}
int timeline_category(int i){return categories&&i>=0&&i<count_?categories[i]:-1;}
int timeline_category_count(int c){if(!categories)return 0;int n=0;for(int i=0;i<count_;++i)if(categories[i]==c)++n;return n;}
int timeline_category_position(void){int n=0,c=timeline_category(index_);if(c<0)return 0;for(int i=0;i<=index_;++i)if(categories[i]==c)++n;return n;}
void timeline_next(int c,int direction){if(!categories)return;for(int n=1;n<=count_;++n){int i=(index_+direction*n%count_+count_)%count_;if(categories[i]==c){timeline_select(i);return;}}}
const char *timeline_category_name(int c){return c==0?"EXPRESSION":c==1?"IDLE / LOOP":"GESTURE";}

void timeline_filter(int (*available)(const char *)){
 if(!categories)return;
 for(int i=0;i<count_;++i){V vars=get(at(timelines,i),"variableList");int usable=0;
  for(unsigned j=0;j<count(vars);++j){V v=at(vars,j),frames=get(v,"frameList");if(!available(str(get(v,"label"))))continue;
   for(unsigned k=0;k<count(frames);++k)if(prop(at(frames,k),"type",0)!=0){usable=1;break;}
  }
  if(!usable)categories[i]=-1;
 }
}

void timeline_focus_category(int c){if(c<0||c>2)return;if(ui_slots[c]>=0&&ui_slots[c]<lane_count&&lanes[ui_slots[c]].index>=0){focus=ui_slots[c];index_=lanes[focus].index;}else timeline_next(c,1);}
