#pragma once
#include <stdint.h>
#ifdef ART3M1S_HOST_GXM
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/log.h>
#ifdef __cplusplus
}
#endif
typedef struct {uint64_t wall,clocks;} HostThreadPerf;
static inline void host_thread_perf(const char *name,HostThreadPerf *p,int force) {
    uint64_t now=sceKernelGetProcessTimeWide();
    if(p->wall && !force && now-p->wall<5000000)return;
    SceKernelThreadInfo info={0};info.size=sizeof(info);
    int tid=sceKernelGetThreadId();
    if(sceKernelGetThreadInfo(tid,&info)<0)return;
    // SDK defines runClocks as clock cycles. Do not label this as CPU percent.
    if(p->wall)av_log(NULL,AV_LOG_INFO,"[thread-perf] role=%s tid=%d cpu=%d last_cpu=%d affinity=%x priority=%d wall_us=%llu run_clocks=%llu\n",name,tid,info.currentCpuId,info.lastExecutedCpuId,info.currentCpuAffinityMask,info.currentPriority,(unsigned long long)(now-p->wall),(unsigned long long)(info.runClocks-p->clocks));
    p->wall=now;p->clocks=info.runClocks;
}
#else
typedef struct {uint64_t unused;} HostThreadPerf;
static inline void host_thread_perf(const char *name,HostThreadPerf *p,int force){(void)name;(void)p;(void)force;}
#endif
