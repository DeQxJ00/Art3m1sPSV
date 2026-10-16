#include "parameter.h"
/* PCSG00341 81192C3E. Preserve the VFP subtract/multiply/divide order. */
void em_parameter_set(void *player,EmParameter *p,uint32_t mode,float value){
 (void)player;p->mode=mode;
 if(p->begin==p->end||p->division<=0){p->frame=0;return;}
 if(p->discretize)value=(float)(int32_t)value;
 float low=p->begin;if(p->end<low)low=p->end;
 float high=p->begin;if(high<p->end)high=p->end;
 float selected=low;if(low<value)selected=value;
 if(selected<high)high=selected;
 p->frame=((high-p->begin)*p->division)/(p->end-p->begin);
}
