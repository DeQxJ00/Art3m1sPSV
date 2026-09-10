#pragma once
#include <stdint.h>
#ifdef DIRECT_RESOURCE_LEDGER
#include <psp2/kernel/processmgr.h>
#ifdef __cplusplus
extern "C" {
#endif
void host_load_timing_log(const char *,const char *,uint64_t,uint64_t,uint64_t,int);
#ifdef __cplusplus
}
#endif
static inline uint64_t host_load_clock(void){return sceKernelGetProcessTimeWide();}
static inline void host_load_report(const char *op,const char *path,uint64_t start,uint64_t acquired,uint64_t end,int result){
    /* Bounded slow-operation samples, emitted after releasing resource locks.
       Nested operations overlap; these times must not be summed together. */
    static unsigned samples;
    if(end-start>=20000 && __atomic_fetch_add(&samples,1,__ATOMIC_RELAXED)<64)
        host_load_timing_log(op,path,acquired-start,end-acquired,end,result);
}
#else
static inline uint64_t host_load_clock(void){return 0;}
static inline void host_load_report(const char *op,const char *path,uint64_t start,uint64_t acquired,uint64_t end,int result){
    (void)op;(void)path;(void)start;(void)acquired;(void)end;(void)result;
}
#endif
