#pragma once
#include "gpu.hpp"
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

namespace direct {
// Preserve the existing gate semantics. The async logger makes this diagnostic
// safe to emit after a slow filesystem call, without performing another write.
inline int diagnostic_stat(const char* path,SceIoStat* stat){
    const auto started=sceKernelGetProcessTimeWide();
    const int result=sceIoGetstat(path,stat);
    const auto ended=sceKernelGetProcessTimeWide();
    if(ended-started>=20000)
        log("[gate-io-slow] path=%s elapsed_us=%llu at_us=%llu result=%d",
            path,(unsigned long long)(ended-started),(unsigned long long)ended,result);
    return result;
}
}
