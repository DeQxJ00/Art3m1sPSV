#ifndef EM_PATCH_OPS_H
#define EM_PATCH_OPS_H
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
/* Fixed 4x4 control patches. Preserve the scalar multiply/add order. */
static inline void em_patch_accumulate(float *out,const float *a,const float *b,float f,int exact,int add){
#if defined(__ARM_NEON)
 float32x4_t af=vdupq_n_f32(1-f),bf=vdupq_n_f32(f);
 for(unsigned k=0;k<32;k+=4){float32x4_t p=vld1q_f32(a+k);
  if(!exact)p=vaddq_f32(vmulq_f32(p,af),vmulq_f32(vld1q_f32(b+k),bf));
  if(add)p=vaddq_f32(vld1q_f32(out+k),p);vst1q_f32(out+k,p);
 }
#else
 for(unsigned k=0;k<32;++k){float p=exact?a[k]:a[k]*(1-f)+b[k]*f;if(add)out[k]+=p;else out[k]=p;}
#endif
}
static inline void em_patch_combine(float *out,const float *parent){
#if defined(__ARM_NEON)
 const float columns[4]={0,1/3.0f,2/3.0f,1};float32x4_t x=vld1q_f32(columns);
 for(unsigned row=0;row<4;++row){float32x4x2_t p=vld2q_f32(out+8*row),q=vld2q_f32(parent+8*row);
  p.val[0]=vsubq_f32(vaddq_f32(p.val[0],q.val[0]),x);
  p.val[1]=vsubq_f32(vaddq_f32(p.val[1],q.val[1]),vdupq_n_f32(row/3.0f));vst2q_f32(out+8*row,p);
 }
#else
 for(unsigned k=0;k<16;++k){out[2*k]=(out[2*k]+parent[2*k])-(k%4)/3.0f;out[2*k+1]=(out[2*k+1]+parent[2*k+1])-(k/4)/3.0f;}
#endif
}
static inline void em_patch_transform(float *out,const float *in,float a,float b,float c,float d,float tx,float ty){
#if defined(__ARM_NEON)
 for(unsigned k=0;k<16;k+=4){float32x4x2_t p=vld2q_f32(in+2*k),q;
  q.val[0]=vaddq_f32(vaddq_f32(vmulq_n_f32(p.val[1],b),vmulq_n_f32(p.val[0],a)),vdupq_n_f32(tx));
  q.val[1]=vaddq_f32(vaddq_f32(vmulq_n_f32(p.val[1],d),vmulq_n_f32(p.val[0],c)),vdupq_n_f32(ty));vst2q_f32(out+2*k,q);
 }
#else
 for(unsigned k=0;k<16;++k){float x=in[2*k],y=in[2*k+1];out[2*k]=(b*y+a*x)+tx;out[2*k+1]=(d*y+c*x)+ty;}
#endif
}
#endif
