#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef DIRECT_RESOURCE_LEDGER
#ifdef __cplusplus
extern "C" {
#endif
void art3m1s_resource_event(uint32_t,uint32_t,int64_t,int64_t,int64_t);
#ifdef __cplusplus
}
#endif
#endif
/* Explicit media buffers only; codec/avformat internal allocations are excluded. */
static inline void host_media_resource_event(int64_t live,int64_t reserved){
#ifdef DIRECT_RESOURCE_LEDGER
    art3m1s_resource_event(0,8,live,reserved,0);
#else
    (void)live;(void)reserved;
#endif
}
static inline void host_media_resource_commit(size_t *charge,size_t requested,int succeeded){
    *charge=succeeded?requested:0;
    host_media_resource_event((int64_t)*charge,-(int64_t)requested);
}
static inline void host_media_resource_release(size_t *charge){
    if(*charge)host_media_resource_event(-(int64_t)*charge,0);
    *charge=0;
}
/* Audio integration allocations with a known immutable requested size. Codec
   internals and static PCM arrays are deliberately excluded from heap totals. */
static inline void host_audio_resource_event(int64_t live,int64_t reserved){
#ifdef DIRECT_RESOURCE_LEDGER
    art3m1s_resource_event(0,10,live,reserved,0);
#else
    (void)live;(void)reserved;
#endif
}
static inline void *host_audio_alloc(size_t bytes,int zero){
    host_audio_resource_event(0,(int64_t)bytes);
    void *p=zero?calloc(1,bytes):malloc(bytes);
    host_audio_resource_event(p?(int64_t)bytes:0,-(int64_t)bytes);return p;
}
static inline void host_audio_free(void *p,size_t bytes){
    if(!p)return;
    free(p);host_audio_resource_event(-(int64_t)bytes,0);
}
