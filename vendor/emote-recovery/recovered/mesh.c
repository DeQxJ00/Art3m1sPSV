#include "mesh.h"
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
static void basis(float t,float b[4]){
 float q=1.0f-t;
 b[0]=(q*q)*q;b[1]=((3.0f*q)*q)*t;
 b[2]=((3.0f*q)*t)*t;b[3]=(t*t)*t;
}
EmPoint em_mesh_patch(const EmPoint *p,float u,float v){
 float x[4],y[4],a[4],b[4];basis(u,a);basis(v,b);
 for(int i=0;i<4;++i){
  x[i]=(p[i].x*a[i])*b[0];y[i]=(p[i].y*a[i])*b[0];
  for(int j=1;j<4;++j){x[i]=x[i]+(p[j*4+i].x*a[i])*b[j];y[i]=y[i]+(p[j*4+i].y*a[i])*b[j];}
 }
 return (EmPoint){(x[2]+x[3])+(x[0]+x[1]),(y[2]+y[3])+(y[0]+y[1])};
}
EmPoint em_mesh_world(const EmWorldPatch *p,EmPoint point){
 float x=point.x+p->offset[0],y=point.y+p->offset[1];
 float u=x*p->inverse[0]+y*p->inverse[1],v=x*p->inverse[2]+y*p->inverse[3];
 return em_mesh_patch(p->control,u,v);
}
/* Batched world sampling derived from 8119F190; retain its reduction order. */
void em_mesh_world_many(const EmWorldPatch *p,EmPoint *points,unsigned count){
#if defined(__ARM_NEON)
 /* Four independent points per SIMD vector; retain original multiply/add
  * and column reduction order. This is a batching optimization of 8119F190,
  * not a claim that the original used point-parallel SIMD. */
 unsigned i=0;
 for(;i+4<=count;i+=4){
  float32x4x2_t xy=vld2q_f32((const float*)(points+i));
  float32x4_t x=vaddq_f32(xy.val[0],vdupq_n_f32(p->offset[0])),y=vaddq_f32(xy.val[1],vdupq_n_f32(p->offset[1]));
  /* Full-vector operands avoid ARM32 scalar-lane register spills without
   * regrouping any floating-point operations. Keep -ffp-contract=off. */
  float32x4_t u=vaddq_f32(vmulq_f32(x,vld1q_dup_f32(&p->inverse[0])),vmulq_f32(y,vld1q_dup_f32(&p->inverse[1])));
  float32x4_t v=vaddq_f32(vmulq_f32(x,vld1q_dup_f32(&p->inverse[2])),vmulq_f32(y,vld1q_dup_f32(&p->inverse[3])));
  float32x4_t a[4],b[4],q=vsubq_f32(vdupq_n_f32(1),u),r=vsubq_f32(vdupq_n_f32(1),v);
  a[0]=vmulq_f32(vmulq_f32(q,q),q);a[1]=vmulq_f32(vmulq_f32(vmulq_f32(q,vdupq_n_f32(3)),q),u);a[2]=vmulq_f32(vmulq_f32(vmulq_f32(q,vdupq_n_f32(3)),u),u);a[3]=vmulq_f32(vmulq_f32(u,u),u);
  b[0]=vmulq_f32(vmulq_f32(r,r),r);b[1]=vmulq_f32(vmulq_f32(vmulq_f32(r,vdupq_n_f32(3)),r),v);b[2]=vmulq_f32(vmulq_f32(vmulq_f32(r,vdupq_n_f32(3)),v),v);b[3]=vmulq_f32(vmulq_f32(v,v),v);
  /* Constant indices and memory broadcasts avoid low-lane register
   * pressure. Compiler-only barriers keep broadcasts local; these emit no
   * hardware fence and do not synchronize CPU/GPU ownership. */
  float32x4_t rx,ry,lowx,lowy,highx,highy;
  __asm__ volatile("" ::: "memory");
  rx=vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[0].x)),b[0]);
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[4].x)),b[1]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[8].x)),b[2]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[12].x)),b[3]));
  __asm__ volatile("" ::: "memory");
  ry=vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[0].y)),b[0]);
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[4].y)),b[1]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[8].y)),b[2]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[0],vld1q_dup_f32(&p->control[12].y)),b[3]));
  lowx=rx;lowy=ry;
  __asm__ volatile("" ::: "memory");
  rx=vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[1].x)),b[0]);
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[5].x)),b[1]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[9].x)),b[2]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[13].x)),b[3]));
  __asm__ volatile("" ::: "memory");
  ry=vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[1].y)),b[0]);
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[5].y)),b[1]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[9].y)),b[2]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[1],vld1q_dup_f32(&p->control[13].y)),b[3]));
  lowx=vaddq_f32(lowx,rx);lowy=vaddq_f32(lowy,ry);
  __asm__ volatile("" ::: "memory");
  rx=vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[2].x)),b[0]);
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[6].x)),b[1]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[10].x)),b[2]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[14].x)),b[3]));
  __asm__ volatile("" ::: "memory");
  ry=vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[2].y)),b[0]);
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[6].y)),b[1]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[10].y)),b[2]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[2],vld1q_dup_f32(&p->control[14].y)),b[3]));
  highx=rx;highy=ry;
  __asm__ volatile("" ::: "memory");
  rx=vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[3].x)),b[0]);
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[7].x)),b[1]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[11].x)),b[2]));
  __asm__ volatile("" ::: "memory");
  rx=vaddq_f32(rx,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[15].x)),b[3]));
  __asm__ volatile("" ::: "memory");
  ry=vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[3].y)),b[0]);
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[7].y)),b[1]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[11].y)),b[2]));
  __asm__ volatile("" ::: "memory");
  ry=vaddq_f32(ry,vmulq_f32(vmulq_f32(a[3],vld1q_dup_f32(&p->control[15].y)),b[3]));
  highx=vaddq_f32(highx,rx);highy=vaddq_f32(highy,ry);
  xy.val[0]=vaddq_f32(highx,lowx);xy.val[1]=vaddq_f32(highy,lowy);vst2q_f32((float*)(points+i),xy);
 }
 for(;i<count;++i)points[i]=em_mesh_world(p,points[i]);
#else
 for(unsigned i=0;i<count;++i)points[i]=em_mesh_world(p,points[i]);
#endif
}

