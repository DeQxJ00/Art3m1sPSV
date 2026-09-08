#pragma once
#include <stdint.h>
typedef struct {unsigned size;int currentCpuId,lastExecutedCpuId,currentCpuAffinityMask,currentPriority;uint64_t runClocks;} SceKernelThreadInfo;
static inline int sceKernelGetThreadId(void){return 1;}
static inline int sceKernelGetThreadInfo(int id,SceKernelThreadInfo *info){(void)id;(void)info;return -1;}
