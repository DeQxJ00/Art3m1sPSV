#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Registry IDs, not pointers. Create/use are render-owner only. release/destroy
 * merely mark retired and are safe on other threads. collect is scene-outside. */
uint64_t art3m1s_recovery_create(const char *path,const uint8_t *data,size_t bytes,uint32_t width,uint32_t height);
void art3m1s_recovery_destroy(uint64_t instance);
int art3m1s_recovery_command(uint64_t instance,uint32_t op,const char *label,const float args[4],uint32_t flags);
int art3m1s_recovery_query(uint64_t instance,uint32_t op,const char *label,float *out);
int art3m1s_recovery_advance(uint64_t instance,float frames);
uint64_t art3m1s_recovery_capture(uint64_t instance);
int art3m1s_recovery_packet_prepare(uint64_t packet);
void art3m1s_recovery_packet_release(uint64_t packet);
int art3m1s_recovery_packet_draw(uint64_t packet,const float affine[6],float opacity,const float *clip);
int art3m1s_recovery_collect(void);
int art3m1s_recovery_shutdown(void);
#ifdef __cplusplus
}
#endif
