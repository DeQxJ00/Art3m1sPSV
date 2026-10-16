#include "motion_events.h"
/* 8119C098..C0BA / 8119B070..B08C: unsigned cursor limit and asymmetric equality. */
int em_motion_event_forward(uint32_t n,uint32_t i,float next,float now){return n && i<n-2 && next<=now;}
int em_motion_event_reverse(uint32_t n,float current,float now){return n && now<current;}
int em_motion_event_push(EmMotionEventQueue*q,EmMotionEvent e){if(!q||!q->records||q->count>=q->capacity)return 0;q->records[q->count++]=e;return 1;}
/* 81197D30: child events precede already queued parent events. */
int em_motion_event_prepend(EmMotionEventQueue*p,EmMotionEventQueue*c){if(!p||!c||!p->records||!c->records||p==c||p->count>p->capacity||c->count>c->capacity||c->count>p->capacity-p->count)return 0;size_t n=c->count;for(size_t i=p->count;i;i--)p->records[i+n-1]=p->records[i-1];for(size_t i=0;i<n;i++)p->records[i]=c->records[i];p->count+=n;c->count=0;return 1;}
/* 8119754A: reload end after notification, so a callback may append without realloc. */
int em_motion_event_dispatch(EmMotionEventQueue*q,void(*f)(void*,const EmMotionEvent*),void*u){if(!q||!f||!q->records||q->count>q->capacity)return 0;for(size_t i=0;i<q->count;i++){EmMotionEvent e=q->records[i];if(e.type<=1)f(u,&e);}q->count=0;return 1;}
static int publish(EmMotionEventCursor*c,EmMotionEventPosition*p,EmMotionEventQueue*q,int seeking){
 const EmMotionEventFrame*f=&c->frames[c->index];
 if(c->tag){
  if(f->type!=1)return 1;
  size_t need=(f->sync!=0)+(f->action&&*f->action?1:0);if(need>q->capacity-q->count)return 0;
  if(f->align&&(!seeking||p->time==f->time)){p->time=f->time;p->aligned=1;}
  if(f->sync){p->time=f->time;p->synced=1;em_motion_event_push(q,(EmMotionEvent){1,0,0});}
  if(f->action&&*f->action)em_motion_event_push(q,(EmMotionEvent){0,"",f->action});
 }else if((f->mask&0x40000)&&(!seeking||p->time==f->time))return em_motion_event_push(q,(EmMotionEvent){0,c->layer?c->layer:"",f->action});
 return 1;
}
int em_motion_event_seek(EmMotionEventCursor*c,EmMotionEventPosition*p,EmMotionEventQueue*q){
 if(!c||!p||!q||!c->frames||!q->records||q->count>q->capacity||c->count<2||p->time<c->frames[0].time)return 0;
 uint32_t i=0;while(i+1<c->count&&c->frames[i].time!=p->time&&c->frames[i+1].time<=p->time)i++;if(i>c->count-2)i=c->count-2;c->index=i;return publish(c,p,q,1);
}
int em_motion_event_step(EmMotionEventCursor*c,EmMotionEventPosition*p,EmMotionEventQueue*q,int reverse){
 if(!c||!p||!q||!c->frames||!q->records||q->count>q->capacity||c->count<2||c->index>c->count-2||p->time<c->frames[0].time)return 0;
 if(reverse){while(em_motion_event_reverse(c->count,c->frames[c->index].time,p->time)){if(!c->index)return 0;--c->index;if(!publish(c,p,q,0))return 0;}}
 else while(em_motion_event_forward(c->count,c->index,c->frames[c->index+1].time,p->time)){++c->index;if(!publish(c,p,q,0))return 0;}
 return 1;
}
