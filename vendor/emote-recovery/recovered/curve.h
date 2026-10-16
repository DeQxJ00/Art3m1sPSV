#ifndef EM_CURVE_H
#define EM_CURVE_H
#include <stdint.h>
typedef struct {float x,y,second;} EmCurveNode;
typedef struct {uint32_t reserved;EmCurveNode *begin,*end,*capacity_end;} EmCurveBlock;
typedef struct {uint32_t reserved[4];EmCurveBlock *blocks;} EmCurve;
/* 8118ADFA: caller supplies valid cursor and x inside the curve domain. */
float em_curve_sample(const EmCurve *curve,int32_t *block,int32_t *segment,float x);
uint32_t em_color_interpolate(uint32_t from,uint32_t to,float ratio); /* 811944B0 */
/* 81199014 layout, retained for direct ARM oracle comparison. */
typedef struct {
 uint32_t reserved[4];
 float *x,*x_end,*x_capacity;uint32_t x_reserved;
 float *y,*y_end,*y_capacity;uint32_t y_reserved;
 float *times,*times_end,*times_capacity;uint32_t times_reserved;
 EmCurveBlock *blocks;
} EmPath;
typedef struct {float x,y;} EmPathPoint;
EmPathPoint em_path_sample(const EmPath*,int32_t *segment,int32_t *node,float time);
#endif
