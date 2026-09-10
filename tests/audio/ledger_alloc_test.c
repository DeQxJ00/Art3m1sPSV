#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
static int fail_alloc;
static void *test_malloc(size_t n){return fail_alloc?NULL:malloc(n);}
static void *test_calloc(size_t n,size_t s){return fail_alloc?NULL:calloc(n,s);}
#define malloc test_malloc
#define calloc test_calloc
#include "../../host/resource_ledger.h"
#undef malloc
#undef calloc
static int64_t live,reserved;
void art3m1s_resource_event(uint32_t region,uint32_t owner,int64_t l,int64_t r,int64_t retired){
    assert(region==0&&owner==10&&retired==0);
    live+=l;reserved+=r;assert(live>=0&&reserved>=0);
}
int main(void){
    for(int zero=0;zero<2;zero++){
        fail_alloc=1;assert(!host_audio_alloc(8192,zero));assert(!live&&!reserved);
        host_audio_free(NULL,8192);assert(!live&&!reserved);
        fail_alloc=0;unsigned char *p=host_audio_alloc(8192,zero);assert(p&&live==8192&&!reserved);
        if(zero)for(int i=0;i<8192;i++)assert(!p[i]);
        host_audio_free(p,8192);assert(!live&&!reserved);
    }
}
