#pragma once
#include "gpu.hpp"
#include "status_cache.hpp"
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

namespace direct {
// First use observes the actual status. Subsequent reads use a snapshot refreshed
// by the logger about every five seconds (longer if worker I/O is delayed).
// Slow probes can log safely through the queue without writing on this thread.
inline int query_diagnostic_stat(const char* path,SceIoStat* stat){
    const auto started=sceKernelGetProcessTimeWide();
    const int result=sceIoGetstat(path,stat);
    const auto ended=sceKernelGetProcessTimeWide();
    if(ended-started>=20000)
        log("[gate-io-slow] path=%s elapsed_us=%llu at_us=%llu result=%d",
            path,(unsigned long long)(ended-started),(unsigned long long)ended,result);
    return result;
}
inline StatusCache<SceIoStat> diagnosticStatuses(query_diagnostic_stat);
inline int diagnostic_stat(const char* path,SceIoStat* stat){return diagnosticStatuses.read(path,stat);}
// Called only from the background logger, following its periodic flush request.
inline void refresh_diagnostic_gates(){diagnosticStatuses.refresh();}
}
