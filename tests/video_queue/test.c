#include "../../host/video_queue.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>

static void *producer(void *arg) {
    HostVideoQueue *q=arg;
    for(uint64_t i=0;i<10000;i++) if(!video_queue_push_kind(q,(uint8_t*)&i,(int64_t)i,i%2)) return NULL;
    video_queue_end(q);return NULL;
}
int main(void) {
    HostVideoQueue q;uint64_t pixel,out;int64_t pts;
    assert(video_queue_init(&q,0)<0);
    assert(video_queue_init(&q,17u*1024u*1024u)<0);
    assert(video_queue_init(&q,sizeof(pixel))==0);
    for(pixel=0;pixel<3;pixel++)assert(video_queue_push(&q,(uint8_t*)&pixel,pixel*33));
    assert(video_queue_prefilled(&q));
    assert(video_queue_take(&q,32,(uint8_t*)&out,&pts)==1 && out==0 && pts==0);
    assert(video_queue_take(&q,32,(uint8_t*)&out,&pts)==0);
    video_queue_end(&q);
    assert(video_queue_take(&q,100,(uint8_t*)&out,&pts)==1 && out==2 && pts==66);
    assert(q.dropped==1);
    assert(video_queue_take(&q,100,(uint8_t*)&out,&pts)==2);
    video_queue_destroy(&q);
    for(int round=0;round<20;round++) {
        assert(video_queue_init(&q,sizeof(pixel))==0);
        pthread_t thread;assert(!pthread_create(&thread,NULL,producer,&q));
        int64_t last=-1;
        for(;;) {
            unsigned kind=9;int r=video_queue_take_kind(&q,INT64_MAX,(uint8_t*)&out,&pts,&kind);
            if(r==2)break;
            if(r==1){assert(pts>last && out==(uint64_t)pts&&kind==out%2);last=pts;}
        }
        assert(last==9999);pthread_join(thread,NULL);video_queue_destroy(&q);
    }
    // Stop must wake a producer even when the bounded queue has no free slot.
    assert(video_queue_init(&q,sizeof(pixel))==0);
    for(pixel=0;pixel<3;pixel++)assert(video_queue_push(&q,(uint8_t*)&pixel,pixel));
    pthread_t thread;assert(!pthread_create(&thread,NULL,producer,&q));
    video_queue_stop(&q);pthread_join(thread,NULL);video_queue_destroy(&q);
    puts("video queue: timestamp/drop, final frame, bounded producer, stop/join passed");
}
