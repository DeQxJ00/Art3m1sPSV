#include "curve.h"
/* Direct recovery of PCSG00341 8118ADFA/8118AF14..8118AF80.
 * VFP arithmetic order retained, including the non-fused VMLA operations.
 * No endpoint policy is invented here; the host validates the input domain. */
float em_curve_sample(const EmCurve *curve,int32_t *block,int32_t *segment,float x){
 const EmCurveBlock *b=curve->blocks;int32_t k=*block,j=*segment;
 while(x>b[k].end[-1].x){*block=++k;*segment=j=0;}
 while(x<b[k].begin[0].x){*block=--k;*segment=j=(int32_t)(b[k].end-b[k].begin)-2;}
 const EmCurveNode *n=b[k].begin;
 while(x>n[j+1].x)*segment=++j;
 while(x<n[j].x)*segment=--j;
 float width=n[j+1].x-n[j].x;
 float t=(x-n[j].x)/width,q=1-t;
 float t3=(t*t)*t,q3=(q*q)*q;
 float linear=q*n[j].y;linear=linear+t*n[j+1].y;
 float curve_value=(q3-q)*n[j].second;curve_value=curve_value+(t3-t)*n[j+1].second;
 curve_value=((width*width)*curve_value)/6;
 return linear+curve_value;
}
uint32_t em_color_interpolate(uint32_t from,uint32_t to,float ratio){
 float scaled=ratio*256.0f;
 /* VCVT.U32.F32 saturates; an out-of-range C cast would be undefined. */
 uint32_t t=!(scaled>0)?0:scaled>=4294967296.0f?UINT32_MAX:(uint32_t)scaled;
 uint32_t q=256u-t;
 uint32_t rb=((t*(to&0x00ff00ffu)+q*(from&0x00ff00ffu))>>8)&0x00ff00ffu;
 uint32_t ga=((t*((to>>8)&0x00ff00ffu)+q*((from>>8)&0x00ff00ffu))>>8)&0x00ff00ffu;
 return rb|(ga<<8);
}
/* 8118AB2A. Segment domains are validated by the host; local remapping
 * clamps outside its node range exactly as the original instruction path. */
EmPathPoint em_path_sample(const EmPath *path,int32_t *segment,int32_t *node,float time){
 int32_t k=*segment;
 while(time>path->times[k+1]){*segment=++k;*node=0;}
 while(time<path->times[k]){*segment=--k;*node=(int32_t)(path->blocks[k].end-path->blocks[k].begin)-2;}
 const EmCurveBlock *b=path->blocks+k;
 float t=(time-path->times[k])/(path->times[k+1]-path->times[k]);
 if(t<=b->begin[0].x)t=b->begin[0].y;
 else if(t>b->end[-1].x)t=b->end[-1].y;
 else {EmCurve c={{0},(EmCurveBlock*)b};int32_t block=0;t=em_curve_sample(&c,&block,node,t);}
 float q=1-t,t3=(t*t)*t,q3=(q*q)*q;
 float b1=((q*3)*q)*t,b2=((q*3)*t)*t;
 const float *x=path->x+k*3,*y=path->y+k*3;
 EmPathPoint result={b1*x[1]+q3*x[0],b1*y[1]+q3*y[0]};
 result.x=result.x+b2*x[2];result.y=result.y+b2*y[2];
 result.x=result.x+t3*x[3];result.y=result.y+t3*y[3];return result;
}
