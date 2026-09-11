#pragma once
#ifdef ART3M1S_HOST_CPU3
#ifdef __cplusplus
extern "C" {
#endif
void host_cpu_affinity_init(void);
void host_background_thread_enter(const char *role);
#ifdef __cplusplus
}
#endif
#else
static inline void host_background_thread_enter(const char *role){(void)role;}
#endif
