#include "../../host/load_timing_budget.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static uint32_t shared, accepted;
static void *sample(void *unused) {
    (void)unused;
    for (unsigned i=0;i<1000;++i)
        if (host_load_sample_allowed(&shared,50000000))
            __atomic_fetch_add(&accepted,1,__ATOMIC_RELAXED);
    return NULL;
}
int main(void) {
    uint32_t state=0;
    for (unsigned window=0;window<8;++window) {
        for (unsigned i=0;i<16;++i)
            assert(host_load_sample_allowed(&state,(uint64_t)window*5000000));
        assert(!host_load_sample_allowed(&state,(uint64_t)window*5000000+4999999));
    }
    assert(!host_load_sample_allowed(&state,10000000));
    assert(host_load_sample_allowed(&state,40000000));
    assert(!host_load_sample_allowed(&state,35000000));
    state=(0x00ffffffu<<8)|16;
    assert(host_load_sample_allowed(&state,((uint64_t)0x01000000)*5000000));
    assert(!host_load_sample_allowed(&state,((uint64_t)0x00ffffff)*5000000));
    pthread_t workers[8];
    for(unsigned i=0;i<8;++i)assert(!pthread_create(&workers[i],NULL,sample,NULL));
    for(unsigned i=0;i<8;++i)assert(!pthread_join(workers[i],NULL));
    assert(accepted==16);
    assert(host_load_sample_allowed(&shared,55000000));
    puts("load timing budget: renewal, stale callers, wrap and contention passed");
}
