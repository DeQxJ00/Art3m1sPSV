#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static unsigned allocation,fail_at;
static int64_t live,reserved;
static void *failing_malloc(size_t n){return ++allocation==fail_at?NULL:malloc(n);}
#define DIRECT_RESOURCE_LEDGER 1
#define malloc failing_malloc
#include "../../host/video_queue.h"
#undef malloc
void art3m1s_resource_event(uint32_t region,uint32_t owner,int64_t l,int64_t r,int64_t retired){
    assert(region==0 && owner==8 && retired==0);
    live+=l;reserved+=r;assert(live>=0 && reserved>=0);
}
int main(void){
    HostVideoQueue q;
    for(fail_at=1;fail_at<=VIDEO_QUEUE_SLOTS;fail_at++){
        allocation=0;assert(video_queue_init(&q,4096)<0);
        assert(live==0 && reserved==0); // Includes rollback of earlier slots.
    }
    fail_at=0;allocation=0;assert(video_queue_init(&q,4096)==0);
    assert(live==3*4096 && reserved==0);
    uint8_t pixels[4096]={0},out[4096];int64_t pts;
    assert(video_queue_push(&q,pixels,0));
    assert(video_queue_take(&q,0,out,&pts)==1);
    assert(live==3*4096); // Consuming a frame does not release its reusable storage.
    video_queue_stop(&q);video_queue_destroy(&q);
    assert(live==0 && reserved==0);
    size_t charge=0;host_media_resource_event(0,8192);
    host_media_resource_commit(&charge,8192,0);assert(live==0 && reserved==0);
    host_media_resource_event(0,8192);host_media_resource_commit(&charge,8192,1);
    assert(live==8192);host_media_resource_release(&charge);host_media_resource_release(&charge);
    assert(live==0 && reserved==0);
    puts("media ledger: partial allocation rollback, queue ownership, failure and release passed");
}
