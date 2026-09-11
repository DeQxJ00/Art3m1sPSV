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
#endif
