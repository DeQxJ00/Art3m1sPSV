#pragma once
#include <stdint.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
typedef struct HostAudioEnvelope {
    float gain,target,step;
    int left,stop_at_zero;
} HostAudioEnvelope;
/* Returns consumed stereo frames. A zero-gain stop can shorten the span. */
static inline int host_audio_mix(float *dst,const float *src,int frames,
                                HostAudioEnvelope *e,float left,float right){
    int i=0;
    while(i<frames && e->left>0){
        e->gain+=e->step;if(--e->left==0)e->gain=e->target;
        if(e->stop_at_zero && e->gain<=0)return i;
        dst[2*i]+=src[2*i]*e->gain*left;
        dst[2*i+1]+=src[2*i+1]*e->gain*right;i++;
    }
    if(e->stop_at_zero && e->gain<=0)return i;
    float l=e->gain*left,r=e->gain*right;
#ifdef __ARM_NEON
    const float factors[4]={l,r,l,r};
    float32x4_t gain=vld1q_f32(factors);
    for(;i+2<=frames;i+=2){
        float32x4_t samples=vld1q_f32(src+2*i);
        vst1q_f32(dst+2*i,vaddq_f32(vld1q_f32(dst+2*i),vmulq_f32(samples,gain)));
    }
#endif
    for(;i<frames;i++){dst[2*i]+=src[2*i]*l;dst[2*i+1]+=src[2*i+1]*r;}
    return i;
}
static inline float host_audio_pack(int16_t *dst,const float *src,int count,unsigned *clipped){
    float peak=0;
    for(int i=0;i<count;i++){
        float v=src[i],a=v<0?-v:v;
        if(a>peak)peak=a;
        if(v>1){v=1;(*clipped)++;}else if(v< -1){v=-1;(*clipped)++;}
        dst[i]=(int16_t)(v*32767);
    }
    return peak;
}
