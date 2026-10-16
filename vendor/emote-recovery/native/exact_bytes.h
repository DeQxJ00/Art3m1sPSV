#ifndef EM_EXACT_BYTES_H
#define EM_EXACT_BYTES_H
#include <stddef.h>
#include <stdint.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
/* Exact byte equality for small, frequently equal dependency records. Read
 * complete vectors only; the remainder never touches the following record. */
static inline int em_exact_bytes(const void *left,const void *right,size_t count){
 const uint8_t *a=left,*b=right;
#if defined(__ARM_NEON)
 uint8x16_t diff=vdupq_n_u8(0);
 for(;count>=16;count-=16,a+=16,b+=16)diff=vorrq_u8(diff,veorq_u8(vld1q_u8(a),vld1q_u8(b)));
 uint32x4_t words=vreinterpretq_u32_u8(diff);
 uint32x2_t halves=vorr_u32(vget_low_u32(words),vget_high_u32(words));
 if(vget_lane_u32(halves,0)|vget_lane_u32(halves,1))return 0;
#endif
 for(size_t i=0;i<count;++i)if(a[i]!=b[i])return 0;
 return 1;
}
#endif
