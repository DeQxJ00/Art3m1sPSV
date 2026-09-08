#pragma once
#include <stddef.h>
int host_launcher_select(char *selected,size_t capacity);
void host_loading_show(int stage,const char *detail);
void host_loading_finish(void);
