#include "cpu_affinity.h"
#include "cpu_affinity_policy.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <libavutil/log.h>
#include <pthread.h>
// Initialized on main before creating any opted-in worker. Never polled in a
// frame, decode loop or audio callback; changing the flag requires a restart.
static int disabled=1;
static int main_thread=-1,key_ready=0;
static pthread_key_t configured_key;
static void *probe_cpu3(void *unused){
    (void)unused;host_background_thread_enter("capability-probe");return NULL;
}
void host_cpu_affinity_init(void){
    main_thread=sceKernelGetThreadId();
    key_ready=pthread_key_create(&configured_key,NULL)==0;
    SceIoStat stat={0};int r=sceIoGetstat("ux0:data/art3m1s-gxm/cpu3.off",&stat);
    // ENOENT is the only expected opt-in result. An I/O failure is not permission.
    disabled=!key_ready||(unsigned)r!=0x80010002u;
    av_log(NULL,AV_LOG_INFO,"[cpu3-policy] background_opt_in=%d flag_result=%08x main_audio_unchanged=1; optional CapUnlocker, restart to change cpu3.off\n",!disabled,(unsigned)r);
    // A short-lived thread verifies kernel support without touching main's
    // affinity. It performs no workload and never pins exclusively to CPU3.
    if(!disabled){pthread_t probe;int created=pthread_create(&probe,NULL,probe_cpu3,NULL);
        if(!created)pthread_join(probe,NULL);
        else av_log(NULL,AV_LOG_INFO,"[cpu3-policy] capability_probe_create=%d; workers will still verify individually\n",created);}
}
static int query_mask(int *mask){
    SceKernelThreadInfo info={0};info.size=sizeof(info);
    int r=sceKernelGetThreadInfo(sceKernelGetThreadId(),&info);
    if(r>=0)*mask=info.currentCpuAffinityMask;return r;
}
static int set_mask(int mask){return sceKernelChangeThreadCpuAffinityMask(0,mask);}
void host_background_thread_enter(const char *role){
    // FFmpeg buffer callbacks also run on main for a non-threaded decoder.
    // Never change main, and apply this policy only once per OS thread.
    if(sceKernelGetThreadId()==main_thread||!key_ready)return;
    if(key_ready){if(pthread_getspecific(configured_key))return;
        if(pthread_setspecific(configured_key,(void*)1)!=0)return;}
    if(disabled){av_log(NULL,AV_LOG_INFO,"[cpu3-worker] role=%s unchanged=1 reason=disabled\n",role);return;}
    HostCpuPolicyResult r=host_try_cpu3(query_mask,set_mask);
    SceKernelThreadInfo info={0};info.size=sizeof(info);
    int info_result=sceKernelGetThreadInfo(sceKernelGetThreadId(),&info);
    av_log(NULL,AV_LOG_INFO,"[cpu3-worker] role=%s tid=%d before=%x requested=%x after=%x query=%08x apply=%08x verify=%08x restore=%08x enabled=%d cpu=%d last_cpu=%d priority=%d; affinity is eligibility, not exclusive core ownership\n",
        role,sceKernelGetThreadId(),r.before,r.requested,r.after,(unsigned)r.query_result,(unsigned)r.apply_result,
        (unsigned)r.verify_result,(unsigned)r.restore_result,r.enabled,info_result>=0?info.currentCpuId:-1,
        info_result>=0?info.lastExecutedCpuId:-1,info_result>=0?info.currentPriority:-1);
}
