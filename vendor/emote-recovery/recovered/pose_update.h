#ifndef EM_POSE_UPDATE_H
#define EM_POSE_UPDATE_H
#include <math.h>
/* 81193E04: caller owns frame/seek dirty flags. Empty slots return before
 * touching the saved ratio; HOLD does not consume a tween ratio. */
static inline int em_pose_update_gate(float *previous,int force,int dirty,int empty,int tween,float ratio){
 int update=force||dirty;
 if(empty)return update;
 if(tween){if(fabsf(*previous-ratio)>=0x1p-23f)update=1;
  if(fabsf(ratio)<0x1p-23f||update)*previous=ratio;}
 return update;
}
#endif
