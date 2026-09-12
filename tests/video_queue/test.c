#include "../../host/video_queue.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#include <sched.h>
#ifdef VIDEO_QUEUE_VITA_PROBE
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
unsigned int _newlib_heap_size_user=16*1024*1024;
static int probe_puts(const char *s){
    FILE *f=fopen("ux0:data/art3m1s-video-queue-probe/result.log","a");
    if(!f)return -1;fputs(s,f);fputc('\n',f);fclose(f);return 0;
}
#define puts probe_puts
#define STRESS_ROUNDS 4
#define STRESS_FRAMES 1000
#else
#define STRESS_ROUNDS 20
#define STRESS_FRAMES 10000
#endif

static void *producer(void *arg) {
    HostVideoQueue *q=arg;
    for(uint64_t i=0;i<STRESS_FRAMES;i++) {
        if(i%2){if(!video_queue_push_kind(q,(uint8_t*)&i,(int64_t)i,1))return NULL;}
        else{
            uint8_t *slot=video_queue_write_begin(q);if(!slot)return NULL;
            memcpy(slot,&i,sizeof(i));sched_yield();
            if(!video_queue_write_commit(q,(int64_t)i,0))return NULL;
        }
    }
    video_queue_end(q);return NULL;
}
int main(void) {
#ifdef VIDEO_QUEUE_VITA_PROBE
    sceIoMkdir("ux0:data/art3m1s-video-queue-probe",0777);
    FILE *f=fopen("ux0:data/art3m1s-video-queue-probe/result.log","w");if(!f)return 2;fclose(f);
    puts("video queue probe START");
#endif
    HostVideoQueue q;uint64_t pixel,out;int64_t pts;
    assert(video_queue_init(&q,0)<0);
    assert(video_queue_init(&q,17u*1024u*1024u)<0);
    uint64_t external[VIDEO_QUEUE_SLOTS]={1,2,3};
    uint8_t* slots[VIDEO_QUEUE_SLOTS]={(uint8_t*)&external[0],(uint8_t*)&external[1],(uint8_t*)&external[2]};
    uint8_t* bad_slots[VIDEO_QUEUE_SLOTS]={slots[0],slots[0],slots[2]};
    assert(video_queue_init_storage(&q,sizeof(pixel),bad_slots)<0);
    bad_slots[1]=slots[0]+1;assert(video_queue_init_storage(&q,sizeof(pixel),bad_slots)<0);
    bad_slots[1]=NULL;assert(video_queue_init_storage(&q,sizeof(pixel),bad_slots)<0);
    assert(video_queue_init_storage(&q,sizeof(pixel),slots)==0);
    assert(!q.owns_pixels&&video_queue_write_begin(&q)==slots[0]);
    pixel=77;memcpy(slots[0],&pixel,sizeof(pixel));assert(video_queue_write_commit(&q,0,1));
    const uint8_t* external_loan=NULL;unsigned external_kind=0;
    assert(video_queue_acquire(&q,0,&external_loan,&pts,&external_kind)==1);
    assert(external_loan==slots[0]&&external_kind==1);
    video_queue_stop(&q);assert(*(const uint64_t*)external_loan==77);
    video_queue_release(&q);video_queue_destroy(&q);
    assert(external[0]==77); // destroy must not free or modify external storage.
    assert(video_queue_init(&q,sizeof(pixel))==0);
#ifdef VIDEO_QUEUE_VITA_PROBE
    puts("video queue init passed");
#endif
    for(pixel=0;pixel<3;pixel++)assert(video_queue_push(&q,(uint8_t*)&pixel,pixel*33));
    assert(video_queue_prefilled(&q));
    assert(video_queue_take(&q,32,(uint8_t*)&out,&pts)==1 && out==0 && pts==0);
    assert(video_queue_take(&q,32,(uint8_t*)&out,&pts)==0);
    video_queue_end(&q);
    assert(video_queue_take(&q,100,(uint8_t*)&out,&pts)==1 && out==2 && pts==66);
    assert(q.dropped==1);
    assert(video_queue_take(&q,100,(uint8_t*)&out,&pts)==2);
    video_queue_destroy(&q);
#ifdef VIDEO_QUEUE_VITA_PROBE
    puts("video queue basic timing passed");
#endif
    // A reserved write is not published, and remains the same tail while the
    // consumer releases earlier frames. Stop cancels an unpublished write.
    assert(video_queue_init(&q,sizeof(pixel))==0);pixel=17;
    assert(video_queue_push(&q,(uint8_t*)&pixel,0));
    uint8_t *writing=video_queue_write_begin(&q);assert(writing);
    const uint8_t *loan=NULL;unsigned kind=9;
    assert(video_queue_acquire(&q,0,&loan,&pts,&kind)==1);
    assert(loan!=writing&&*(const uint64_t*)loan==17);
    assert(video_queue_acquire(&q,0,&loan,&pts,&kind)==0);
    video_queue_release(&q);
    assert(video_queue_acquire(&q,100,&loan,&pts,&kind)==0);
    pixel=23;memcpy(writing,&pixel,sizeof(pixel));assert(video_queue_write_commit(&q,1,1));
    assert(video_queue_acquire(&q,100,&loan,&pts,&kind)==1);
    assert(loan==writing&&*(const uint64_t*)loan==23&&kind==1&&pts==1);
    writing=video_queue_write_begin(&q);assert(writing&&writing!=loan);
    video_queue_stop(&q);memset(writing,0,sizeof(pixel));assert(!video_queue_write_commit(&q,2,0));
    assert(*(const uint64_t*)loan==23);video_queue_release(&q);video_queue_destroy(&q);
#ifdef VIDEO_QUEUE_VITA_PROBE
    puts("video queue reservation and stop passed");
#endif
    for(int round=0;round<STRESS_ROUNDS;round++) {
        assert(video_queue_init_storage(&q,sizeof(pixel),round%2?slots:NULL)==0);
        pthread_t thread;assert(!pthread_create(&thread,NULL,producer,&q));
        int64_t last=-1;
        for(;;) {
            unsigned kind=9;const uint8_t *borrowed=NULL;
            int r=video_queue_acquire(&q,INT64_MAX,&borrowed,&pts,&kind);
            if(r==2)break;
#ifdef VIDEO_QUEUE_VITA_PROBE
            if(r==0)sceKernelDelayThread(100); // unlike the desktop, lower-priority pthreads need a blocking yield
#endif
            if(r==1){
                memcpy(&out,borrowed,sizeof(out));assert(pts>last&&out==(uint64_t)pts&&kind==out%2);last=pts;
                // Producer may run and fill other slots, but must never
                // overwrite this one until the consumer finishes reading.
                for(int i=0;i<3;i++){sched_yield();assert(*(const uint64_t*)borrowed==out);}
                video_queue_release(&q);
            }
        }
        assert(last==STRESS_FRAMES-1);pthread_join(thread,NULL);video_queue_destroy(&q);
#ifdef VIDEO_QUEUE_VITA_PROBE
        puts("video queue stress round passed");
#endif
    }
    // Stop must wake a producer even when the bounded queue has no free slot.
    assert(video_queue_init(&q,sizeof(pixel))==0);
    for(pixel=0;pixel<3;pixel++)assert(video_queue_push(&q,(uint8_t*)&pixel,pixel));
    pthread_t thread;assert(!pthread_create(&thread,NULL,producer,&q));
    video_queue_stop(&q);pthread_join(thread,NULL);video_queue_destroy(&q);
    puts("video queue: reserved writes, pinned loans, timestamp/drop, final frame, bounded producer, stop/join passed");
#ifndef VIDEO_QUEUE_VITA_PROBE
    fflush(stdout);
#endif
}
