#pragma once
#include <stddef.h>
typedef struct { size_t size; int size_user,size_cdram,size_phycont; } SceKernelFreeMemorySizeInfo;
static inline int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info) {(void)info;return -1;}
