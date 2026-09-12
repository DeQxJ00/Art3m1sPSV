#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "resource_ledger.h"

/* Bounded CPU frames. Only the consumer may touch the runtime or GPU. */
#define VIDEO_QUEUE_SLOTS 3
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t space;
    uint8_t *pixels[VIDEO_QUEUE_SLOTS];
    int64_t pts[VIDEO_QUEUE_SLOTS];
    unsigned kind[VIDEO_QUEUE_SLOTS]; /* 0=RGBA, 1=packed Y/U/V/mask-Y planes */
    size_t bytes;
    int head, count, stopped, ended;
    unsigned dropped;
    int64_t clock;
} HostVideoQueue;

static int video_queue_init(HostVideoQueue *q, size_t bytes) {
    memset(q,0,sizeof(*q));
    if (!bytes || bytes > (16u*1024u*1024u)/(VIDEO_QUEUE_SLOTS+1)) return -1;
    if (pthread_mutex_init(&q->mutex,NULL)) return -1;
    if (pthread_cond_init(&q->space,NULL)) { pthread_mutex_destroy(&q->mutex); return -1; }
    q->bytes=bytes;
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
static int video_queue_push_kind(HostVideoQueue *q,const uint8_t *pixels,int64_t pts,unsigned kind) {
    pthread_mutex_lock(&q->mutex);
    while(q->count==VIDEO_QUEUE_SLOTS&&!q->stopped) pthread_cond_wait(&q->space,&q->mutex);
    if(q->stopped) {pthread_mutex_unlock(&q->mutex);return 0;}
    int slot=(q->head+q->count)%VIDEO_QUEUE_SLOTS;
    memcpy(q->pixels[slot],pixels,q->bytes);q->pts[slot]=pts;q->kind[slot]=kind;q->count++;
    pthread_mutex_unlock(&q->mutex);return 1;
}
static int video_queue_push(HostVideoQueue *q,const uint8_t *pixels,int64_t pts) {return video_queue_push_kind(q,pixels,pts,0);}
/* 1=frame, 0=not due, 2=EOF; never discard the newest eligible frame. */
static int video_queue_take_kind(HostVideoQueue *q,int64_t clock,uint8_t *out,int64_t *pts,unsigned *kind) {
    pthread_mutex_lock(&q->mutex);
    q->clock=clock;
    while(q->count>1 && q->pts[(q->head+1)%VIDEO_QUEUE_SLOTS]<=clock) {
        q->head=(q->head+1)%VIDEO_QUEUE_SLOTS;q->count--;q->dropped++;
    }
    int result=0;
    if(q->count && q->pts[q->head]<=clock) {
        memcpy(out,q->pixels[q->head],q->bytes);*pts=q->pts[q->head];*kind=q->kind[q->head];
        q->head=(q->head+1)%VIDEO_QUEUE_SLOTS;q->count--;result=1;
        pthread_cond_signal(&q->space);
    } else if(!q->count && q->ended) result=2;
    pthread_mutex_unlock(&q->mutex);return result;
}
static int video_queue_take(HostVideoQueue *q,int64_t clock,uint8_t *out,int64_t *pts) {unsigned kind;return video_queue_take_kind(q,clock,out,pts,&kind);}
/* Call only after joining the producer. */
static void video_queue_destroy(HostVideoQueue *q) {
    for(int i=0;i<VIDEO_QUEUE_SLOTS;i++) if(q->pixels[i]){free(q->pixels[i]);q->pixels[i]=NULL;host_media_resource_event(-(int64_t)q->bytes,0);}
    pthread_cond_destroy(&q->space);pthread_mutex_destroy(&q->mutex);
}
