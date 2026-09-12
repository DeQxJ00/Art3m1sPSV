#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "resource_ledger.h"

/* Bounded single-producer/single-consumer CPU frames. Only the consumer may
 * touch the runtime or GPU. A borrowed head remains counted until release. */
#define VIDEO_QUEUE_SLOTS 3
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t space;
    uint8_t *pixels[VIDEO_QUEUE_SLOTS];
    int64_t pts[VIDEO_QUEUE_SLOTS];
    unsigned kind[VIDEO_QUEUE_SLOTS]; /* 0=RGBA, 1=packed Y/U/V/mask-Y planes */
    size_t bytes;
    int head, count, stopped, ended;
    int writing, write_slot, borrowed, owns_pixels;
    unsigned dropped;
    int64_t clock;
} HostVideoQueue;

/* External slots belong to the render-thread allocation pool. Caller keeps
 * them mapped until producer join AND completion of any consumer/GPU loan. */
static int video_queue_init_storage(HostVideoQueue *q, size_t bytes, uint8_t *const *slots) {
    memset(q,0,sizeof(*q));
    if (!bytes || bytes > (16u*1024u*1024u)/(VIDEO_QUEUE_SLOTS+1)) return -1;
    if(slots)for(int i=0;i<VIDEO_QUEUE_SLOTS;i++){
        const uintptr_t p=(uintptr_t)slots[i];
        if(!p||p>UINTPTR_MAX-bytes)return -1;
        for(int j=0;j<i;j++){
            const uintptr_t other=(uintptr_t)slots[j];
            if(p<other+bytes&&other<p+bytes)return -1;
        }
    }
    if (pthread_mutex_init(&q->mutex,NULL)) return -1;
    if (pthread_cond_init(&q->space,NULL)) { pthread_mutex_destroy(&q->mutex); return -1; }
    q->bytes=bytes;q->owns_pixels=slots==NULL;
    if(slots){for(int i=0;i<VIDEO_QUEUE_SLOTS;i++)q->pixels[i]=slots[i];return 0;}
    for (int i=0;i<VIDEO_QUEUE_SLOTS;i++) {
        host_media_resource_event(0,bytes);
        q->pixels[i]=malloc(bytes);
        host_media_resource_event(q->pixels[i]?(int64_t)bytes:0,-(int64_t)bytes);
        if (!q->pixels[i]) {
            for(int j=0;j<i;j++){free(q->pixels[j]);q->pixels[j]=NULL;host_media_resource_event(-(int64_t)bytes,0);}
            pthread_cond_destroy(&q->space);pthread_mutex_destroy(&q->mutex);return -1;
        }
    }
    return 0;
}
static int video_queue_init(HostVideoQueue *q,size_t bytes){return video_queue_init_storage(q,bytes,NULL);}
static void video_queue_stop(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);q->stopped=1;
    pthread_cond_broadcast(&q->space);pthread_mutex_unlock(&q->mutex);
}
static void video_queue_end(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);q->ended=1;pthread_mutex_unlock(&q->mutex);
}
static int video_queue_done(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);int done=q->stopped||q->ended;
    pthread_mutex_unlock(&q->mutex);return done;
}
static int video_queue_prefilled(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);int ready=q->count>=2||q->ended;
    pthread_mutex_unlock(&q->mutex);return ready;
}
static int64_t video_queue_clock(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);int64_t clock=q->clock;
    pthread_mutex_unlock(&q->mutex);return clock;
}
/* A reserved tail is invisible to the consumer until commit. The producer
 * writes outside the lock; removing ready heads cannot change this tail. */
static uint8_t *video_queue_write_begin(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);
    while(q->count==VIDEO_QUEUE_SLOTS&&!q->stopped) pthread_cond_wait(&q->space,&q->mutex);
    if(q->stopped||q->ended||q->writing) {pthread_mutex_unlock(&q->mutex);return NULL;}
    q->writing=1;q->write_slot=(q->head+q->count)%VIDEO_QUEUE_SLOTS;
    uint8_t *pixels=q->pixels[q->write_slot];
    pthread_mutex_unlock(&q->mutex);return pixels;
}
static int video_queue_write_commit(HostVideoQueue *q,int64_t pts,unsigned kind) {
    pthread_mutex_lock(&q->mutex);
    if(!q->writing){pthread_mutex_unlock(&q->mutex);return 0;}
    q->writing=0;
    if(q->stopped||q->ended){pthread_mutex_unlock(&q->mutex);return 0;}
    int slot=q->write_slot;q->pts[slot]=pts;q->kind[slot]=kind;q->count++;
    pthread_mutex_unlock(&q->mutex);return 1;
}
static int video_queue_push_kind(HostVideoQueue *q,const uint8_t *pixels,int64_t pts,unsigned kind) {
    uint8_t *out=video_queue_write_begin(q);if(!out)return 0;
    memcpy(out,pixels,q->bytes);return video_queue_write_commit(q,pts,kind);
}
static int video_queue_push(HostVideoQueue *q,const uint8_t *pixels,int64_t pts) {return video_queue_push_kind(q,pixels,pts,0);}
/* 1=frame, 0=not due, 2=EOF; never discard the newest eligible frame. */
static int video_queue_acquire(HostVideoQueue *q,int64_t clock,const uint8_t **out,int64_t *pts,unsigned *kind) {
    pthread_mutex_lock(&q->mutex);
    q->clock=clock;
    if(q->borrowed||q->stopped){pthread_mutex_unlock(&q->mutex);return 0;}
    int discarded=0;
    while(q->count>1 && q->pts[(q->head+1)%VIDEO_QUEUE_SLOTS]<=clock) {
        q->head=(q->head+1)%VIDEO_QUEUE_SLOTS;q->count--;q->dropped++;discarded=1;
    }
    if(discarded)pthread_cond_signal(&q->space);
    int result=0;
    if(q->count && q->pts[q->head]<=clock) {
        *out=q->pixels[q->head];*pts=q->pts[q->head];*kind=q->kind[q->head];
        q->borrowed=1;result=1;
    } else if(!q->count && q->ended) result=2;
    pthread_mutex_unlock(&q->mutex);return result;
}
/* Release only after the synchronous upload has stopped reading the bytes.
 * No queue lock is held during that upload, and stop does not revoke a loan. */
static void video_queue_release(HostVideoQueue *q) {
    pthread_mutex_lock(&q->mutex);
    if(q->borrowed){
        q->borrowed=0;q->head=(q->head+1)%VIDEO_QUEUE_SLOTS;q->count--;
        pthread_cond_signal(&q->space);
    }
    pthread_mutex_unlock(&q->mutex);
}
static int video_queue_take_kind(HostVideoQueue *q,int64_t clock,uint8_t *out,int64_t *pts,unsigned *kind) {
    const uint8_t *pixels=NULL;int result=video_queue_acquire(q,clock,&pixels,pts,kind);
    if(result==1){memcpy(out,pixels,q->bytes);video_queue_release(q);}
    return result;
}
static int video_queue_take(HostVideoQueue *q,int64_t clock,uint8_t *out,int64_t *pts) {unsigned kind;return video_queue_take_kind(q,clock,out,pts,&kind);}
/* Call only after joining the producer. */
static void video_queue_destroy(HostVideoQueue *q) {
    for(int i=0;i<VIDEO_QUEUE_SLOTS;i++) if(q->pixels[i]){
        if(q->owns_pixels){free(q->pixels[i]);host_media_resource_event(-(int64_t)q->bytes,0);}
        q->pixels[i]=NULL;
    }
    pthread_cond_destroy(&q->space);pthread_mutex_destroy(&q->mutex);
}
