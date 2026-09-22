#ifndef HOST_VIDEO_CONVERT_H
#define HOST_VIDEO_CONVERT_H
#include <stddef.h>
#include <stdint.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

/* Unscaled, 8-bit planar YUV444 -> straight RGBA. This matches the existing
 * sws_getCachedContext default: limited-range BT.601, no colour-space override.
 * Other pixel formats continue through swscale in video.c. Rows may be padded
 * or bottom-up; the vector loop never reads beyond the visible row width. */
static inline uint8_t host_video_clip(int v) { return v<0?0:v>255?255:(uint8_t)v; }
static inline uint8_t host_video_gray_sample(uint8_t y) {
    return host_video_clip((((int)y-16)*76309+32768) >> 16);
}
#if defined(__ARM_NEON)
static inline uint8x8_t host_video_pack(int32x4_t lo,int32x4_t hi) {
    return vqmovun_s16(vcombine_s16(vqshrn_n_s32(lo,16),vqshrn_n_s32(hi,16)));
}
static inline int32x4_t host_video_luma(int16x4_t y) {
    return vmlaq_n_s32(vdupq_n_s32(32768),vmovl_s16(y),76309);
}
#endif
static inline void host_video_gray_row(const uint8_t *src,uint8_t *dst,int width) {
    int x=0;
#if defined(__ARM_NEON)
    for(;x+8<=width;x+=8) {
        int16x8_t y=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(src+x))),vdupq_n_s16(16));
        vst1_u8(dst+x,host_video_pack(host_video_luma(vget_low_s16(y)),host_video_luma(vget_high_s16(y))));
    }
#endif
    for(;x<width;x++)dst[x]=host_video_gray_sample(src[x]);
}
/* Unscaled swscale's default subsampled YUV -> RGB path shares each chroma
 * sample across two horizontal pixels. Expand into the existing full-size
 * GXM plane layout; do not change shader sampling or alpha semantics. */
static inline void host_video_chroma2_row(const uint8_t *src,uint8_t *dst,int width) {
    int x=0;
#if defined(__ARM_NEON)
    for(;x+16<=width;x+=16){
        uint8x8_t c=vld1_u8(src+x/2);uint8x8x2_t pair=vzip_u8(c,c);
        vst1_u8(dst+x,pair.val[0]);vst1_u8(dst+x+8,pair.val[1]);
    }
#endif
    for(;x<width;x++)dst[x]=src[x/2];
}
static inline void host_video_yuv444_row(const uint8_t *yp,const uint8_t *up,const uint8_t *vp,
                                       const uint8_t *alpha,uint8_t *dst,int width) {
    int x=0;
#if defined(__ARM_NEON)
    for(;x+8<=width;x+=8) {
        int16x8_t y=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(yp+x))),vdupq_n_s16(16));
        int16x8_t u=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(up+x))),vdupq_n_s16(128));
        int16x8_t v=vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(vp+x))),vdupq_n_s16(128));
        int32x4_t yl=host_video_luma(vget_low_s16(y)),yh=host_video_luma(vget_high_s16(y));
        int32x4_t ul=vmovl_s16(vget_low_s16(u)),uh=vmovl_s16(vget_high_s16(u));
        int32x4_t vl=vmovl_s16(vget_low_s16(v)),vh=vmovl_s16(vget_high_s16(v));
        uint8x8x4_t rgba;
        rgba.val[0]=host_video_pack(vmlaq_n_s32(yl,vl,104597),vmlaq_n_s32(yh,vh,104597));
        rgba.val[1]=host_video_pack(vmlaq_n_s32(vmlaq_n_s32(yl,ul,-25675),vl,-53279),
                                    vmlaq_n_s32(vmlaq_n_s32(yh,uh,-25675),vh,-53279));
        rgba.val[2]=host_video_pack(vmlaq_n_s32(yl,ul,132201),vmlaq_n_s32(yh,uh,132201));
        rgba.val[3]=alpha?vld1_u8(alpha+x):vdup_n_u8(255);
        vst4_u8(dst+4*x,rgba);
    }
#endif
    for(;x<width;x++) {
        int y=((int)yp[x]-16)*76309+32768,u=(int)up[x]-128,v=(int)vp[x]-128;
        dst[4*x]=host_video_clip((y+104597*v)>>16);
        dst[4*x+1]=host_video_clip((y-25675*u-53279*v)>>16);
        dst[4*x+2]=host_video_clip((y+132201*u)>>16);
        dst[4*x+3]=alpha?alpha[x]:255;
    }
}

/* Q6 intermediates keep all eight lanes together. Rounding the coefficient
 * products changes RGB by at most one level versus the wide Q16 path over all
 * 256^3 input triples. Saturating additions only affect values already above
 * the final 255 clamp. Alpha is copied unchanged. Keep the wide kernel above
 * as the device-checked fallback and regression oracle. */
static inline int host_video_q6_product(int value,int coefficient) {
    return (value*128*coefficient+16384)>>15;
}
static inline void host_video_yuv444_q6_row(const uint8_t *yp,const uint8_t *up,const uint8_t *vp,
                                          const uint8_t *alpha,uint8_t *dst,int width) {
    int x=0;
#if defined(__ARM_NEON)
    for(;x+8<=width;x+=8) {
        int16x8_t y=vshlq_n_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(yp+x))),vdupq_n_s16(16)),7);
        int16x8_t u=vshlq_n_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(up+x))),vdupq_n_s16(128)),7);
        int16x8_t v=vshlq_n_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(vp+x))),vdupq_n_s16(128)),7);
        int16x8_t l=vqrdmulhq_n_s16(y,19077);
        int16x8_t b=vqrdmulhq_n_s16(u,16525);
        uint8x8x4_t rgba;
        rgba.val[0]=vqrshrun_n_s16(vqaddq_s16(l,vqrdmulhq_n_s16(v,26149)),6);
        rgba.val[1]=vqrshrun_n_s16(vaddq_s16(vaddq_s16(l,vqrdmulhq_n_s16(u,-6419)),vqrdmulhq_n_s16(v,-13320)),6);
        rgba.val[2]=vqrshrun_n_s16(vqaddq_s16(l,vaddq_s16(b,b)),6);
        rgba.val[3]=alpha?vld1_u8(alpha+x):vdup_n_u8(255);
        vst4_u8(dst+4*x,rgba);
    }
#endif
    for(;x<width;x++) {
        int l=host_video_q6_product((int)yp[x]-16,19077),u=(int)up[x]-128,v=(int)vp[x]-128;
        dst[4*x]=host_video_clip((l+host_video_q6_product(v,26149)+32)>>6);
        dst[4*x+1]=host_video_clip((l+host_video_q6_product(u,-6419)+host_video_q6_product(v,-13320)+32)>>6);
        dst[4*x+2]=host_video_clip((l+2*host_video_q6_product(u,16525)+32)>>6);
        dst[4*x+3]=alpha?alpha[x]:255;
    }
}
#endif
