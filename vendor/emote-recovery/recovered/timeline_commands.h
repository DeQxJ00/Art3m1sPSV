#ifndef RECOVERED_TIMELINE_COMMANDS_H
#define RECOVERED_TIMELINE_COMMANDS_H
#include <stddef.h>
#include <math.h>
typedef struct {float time;unsigned disabled;float value,easing;} EmTimelineCommandKey;
typedef int (*EmTimelineCommandSink)(void *,float value,float duration,float easing);
/* PCSG00341 8117F2E0 one-track forward command producer. Requires sorted finite
 * keys with a disabled terminal sentinel and a valid cursor. No allocations.
 * Callback failure is nontransactional. The sink supplies normal/diff routing. */
int em_timeline_commands_forward(const EmTimelineCommandKey *,size_t,size_t *,float,int,EmTimelineCommandSink,void *);
static inline int em_timeline_commands_forward_inline(const EmTimelineCommandKey*k,size_t n,size_t*i,float t,int inclusive,EmTimelineCommandSink sink,void*u){
 if(!k||!i||!sink||n<2||*i>=n||!k[n-1].disabled||!isfinite(t))return 0;
 while(*i<n-1&&(inclusive?k[*i+1].time<=t:k[*i+1].time<t)){
  const EmTimelineCommandKey*old=k+*i;if(!old->disabled&&!sink(u,old->value,0,0))return 0;
  ++*i;const EmTimelineCommandKey*next=k+*i;
  if(!next->disabled){float duration=k[*i+1].time-t-1;if(duration<0)duration=0;if(!sink(u,next->value,duration,next->easing))return 0;}
 }return 1;
}
#endif
