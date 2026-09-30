#pragma once
#include <stddef.h>
typedef struct { size_t size; int currentPriority; } SceKernelThreadInfo;
int sceKernelGetThreadId(void);
int sceKernelChangeThreadPriority(int tid,int priority);
int sceKernelGetThreadInfo(int tid,SceKernelThreadInfo *info);
