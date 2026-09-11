#pragma once
#ifdef ART3M1S_HOST_CPU3
#ifdef __cplusplus
extern "C" {
#endif
void host_cpu_affinity_init(void);
void host_background_thread_enter(const char *role);
int host_cpu_affinity_read_setting(int *enabled);
int host_cpu_affinity_save_setting(int enabled);
#ifdef __cplusplus
}
#endif
#else
static inline void host_background_thread_enter(const char *role){(void)role;}
static inline int host_cpu_affinity_read_setting(int *enabled){(void)enabled;return -1;}
static inline int host_cpu_affinity_save_setting(int enabled){(void)enabled;return -1;}
#endif
