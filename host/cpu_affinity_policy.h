#pragma once
// Optional capability: widen the existing mask, never pin a worker to CPU3.
// Query/set are injected so denied or clamped kernel requests can be verified.
enum { HOST_CPU3_MASK=0x80000, HOST_USER_CPU_MASK=0x70000 };
typedef struct {
    int before,requested,after,query_result,apply_result,verify_result,restore_result,enabled;
} HostCpuPolicyResult;
static inline HostCpuPolicyResult host_try_cpu3(int (*query)(int *),int (*set)(int)){
    HostCpuPolicyResult r={0};r.apply_result=r.verify_result=r.restore_result=-1;
    r.query_result=query(&r.before);r.after=r.before;
    if(r.query_result<0)return r;
    r.requested=(r.before?r.before:HOST_USER_CPU_MASK)|HOST_CPU3_MASK;
    r.apply_result=set(r.requested);r.verify_result=query(&r.after);
    r.enabled=r.apply_result>=0&&r.verify_result>=0&&r.after==r.requested;
    if(!r.enabled&&(r.verify_result<0||r.after!=r.before)){
        r.restore_result=set(r.before);
        query(&r.after);
    }
    return r;
}
