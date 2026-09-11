#include "../../host/cpu_affinity_policy.h"
#include <assert.h>
#include <stdio.h>
static int mask,mode,queries,sets;
static int query(int *out){
    ++queries;if(mode==3||(mode==4&&queries==2))return -9;
    *out=mask;return 0;
}
static int set(int value){
    ++sets;if(mode==1)return -8;
    mask=mode==2?(value&~HOST_CPU3_MASK):value;return 0;
}
static HostCpuPolicyResult run(int before,int m){mask=before;mode=m;queries=sets=0;return host_try_cpu3(query,set);}
int main(void){
    HostCpuPolicyResult r=run(0,0);assert(r.enabled&&mask==0xf0000&&sets==1);
    r=run(0x20000,0);assert(r.enabled&&mask==0xa0000); // Preserve a custom CPU subset.
    r=run(0x70000,1);assert(!r.enabled&&mask==0x70000&&sets==1); // Plugin absent.
    r=run(0,2);assert(!r.enabled&&mask==0&&sets==2&&r.restore_result==0); // Silent clamp.
    r=run(0x70000,3);assert(!r.enabled&&mask==0x70000&&sets==0); // Query fails: no mutation.
    r=run(0x70000,4);assert(!r.enabled&&mask==0x70000&&sets==2); // Verify fails: restore.
    r=run(0xf0000,0);assert(r.enabled&&mask==0xf0000); // Already supported.
    puts("7 CPU affinity capability/fallback cases passed");
}
