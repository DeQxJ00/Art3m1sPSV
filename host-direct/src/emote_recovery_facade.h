#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Registry IDs, not pointers. Create/use are render-owner only. release/destroy
 * merely mark retired and are safe on other threads. collect is scene-outside. */
uint64_t art3m1s_recovery_create(const char *path,const uint8_t *data,size_t bytes,uint32_t width,uint32_t height);
/* CPU-only immutable templates: prepare/release are thread-safe. */
uint64_t art3m1s_recovery_model_prepare(const uint8_t *data,size_t bytes,size_t allocation_limit,size_t *retained_bytes);
/* Shared input must stay immutable/alive through every model, instance and
 * pending-texture backing lease. */
uint64_t art3m1s_recovery_model_prepare_shared(const uint8_t *data,size_t bytes,size_t allocation_limit,size_t *retained_bytes);
void art3m1s_recovery_model_release(uint64_t model);
/* Lease is adopted only on success. release must keep the shared backing alive
 * until invoked, possibly after instance teardown if a packet awaits upload.
 * A null release preserves the source-copying fallback for borrowed models. */
uint64_t art3m1s_recovery_create_prepared(const char *path,uint64_t model,uint32_t width,uint32_t height,void *lease,void (*release)(void*));
void art3m1s_recovery_destroy(uint64_t instance);
int art3m1s_recovery_command(uint64_t instance,uint32_t op,const char *label,const float args[4],uint32_t flags);
int art3m1s_recovery_query(uint64_t instance,uint32_t op,const char *label,float *out);
int art3m1s_recovery_advance(uint64_t instance,float frames);
uint64_t art3m1s_recovery_capture(uint64_t instance);
/* Synchronous batch; distinct instances, max64. CPU evaluation may run on
 * workers, but all packet/texture ownership remains on the render owner. */
void art3m1s_recovery_capture_batch(const uint64_t *instances,uint64_t *packets,unsigned count);
/* Optional two-model overlap. Success returns pending packet IDs; finish on
 * this owner before submitting the frame. Mutation/resource APIs join early
 * if needed. Release/destroy only retire; no worker owns GPU/texture leases.
 * Returns 0 (all outputs zero) when unavailable; use synchronous capture. */
int art3m1s_recovery_capture_begin(const uint64_t *instances,uint64_t *packets,unsigned count);
void art3m1s_recovery_capture_finish(void);
int art3m1s_recovery_packet_prepare(uint64_t packet);
int art3m1s_recovery_packet_source_over(uint64_t packet);
void art3m1s_recovery_packet_release(uint64_t packet);
int art3m1s_recovery_packet_draw(uint64_t packet,const float affine[6],float opacity,const float *clip);
int art3m1s_recovery_collect(void);
int art3m1s_recovery_prepare_resources(void);
/* Per-window cumulative microseconds: capture, replay, prepare, advances;
 * followed by capture/replay counts. Read/reset on owner; no GPU timing. */
void art3m1s_recovery_take_profile(uint64_t values[6]);
void art3m1s_recovery_take_pose_profile(uint64_t out[6]);
void art3m1s_recovery_take_dirty_profile(uint64_t out[7]);
void art3m1s_recovery_take_pose_parts(uint64_t out[10]);
int art3m1s_recovery_shutdown(void);
#ifdef __cplusplus
}
#endif
