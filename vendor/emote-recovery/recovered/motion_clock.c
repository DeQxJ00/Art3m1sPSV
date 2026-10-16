#include "motion_clock.h"
#include <math.h>
static int visit(EmMotionClock*c,EmMotionVisit f,void*u,int op){if(!f(u,c,op))return -1;return c->synced||c->aligned;}
int em_motion_clock_step(EmMotionClock*c,float dt,EmMotionVisit f,void*u){
 if(!c||!f||!isfinite(dt)||!isfinite(c->absolute)||!isfinite(c->time)||!isfinite(c->last)||!isfinite(c->loop)||!isfinite(c->absolute+dt))return 0;c->aligned=0;if((!c->seek&&!c->playing)||c->synced)return 1;int stop;
 if(c->seek){c->seek=0;if(dt<0&&c->absolute==0)c->absolute=c->time=c->last;
  if(c->from_edge){c->from_edge=0;
   if(dt<0){if(c->absolute<c->last){float t=c->time;c->time=c->last;if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;c->time=t;if((stop=visit(c,f,u,EM_MOTION_REVERSE)))return stop>0;}
    else if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;
   }else{float t=c->time;c->time=0;if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;c->time=t;if((stop=visit(c,f,u,EM_MOTION_FORWARD)))return stop>0;}
  }else if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;
 }
 if(!c->hold){c->absolute+=dt;c->time=c->absolute<c->last?c->absolute:c->last;}
 if(dt<0){
  if(c->absolute<0||c->absolute<c->loop){
   if(c->loop>=0){if(c->last<=c->loop)return 0;c->time=c->loop;if((stop=visit(c,f,u,EM_MOTION_REVERSE)))return stop>0;c->time=c->last;if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;
    unsigned n=0;while(c->absolute<c->loop){float next=(c->absolute-c->loop)+c->last;if(++n>65536||next<=c->absolute)return 0;c->absolute=next;}c->time=c->absolute;return visit(c,f,u,EM_MOTION_REVERSE)>=0;
   }c->absolute=c->time=0;c->playing=0;
  }if(!c->hold)return visit(c,f,u,EM_MOTION_REVERSE)>=0;
 }else{
  if(c->absolute>=c->last){
   if(c->loop>=0){if(c->last<=c->loop)return 0;c->time=c->last;if((stop=visit(c,f,u,EM_MOTION_FORWARD)))return stop>0;c->time=c->loop;if((stop=visit(c,f,u,EM_MOTION_SEEK)))return stop>0;
    unsigned n=0;while(c->absolute>=c->last){float next=(c->absolute-c->last)+c->loop;if(++n>65536||next>=c->absolute)return 0;c->absolute=next;}c->time=c->absolute;return visit(c,f,u,EM_MOTION_FORWARD)>=0;
   }c->time=c->last;c->playing=0;
  }if(!c->hold)return visit(c,f,u,EM_MOTION_FORWARD)>=0;
 }return 1;
}
