#ifndef EM_TEXTURE_HOST_H
#define EM_TEXTURE_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { EM_TEXTURE_RGBA8=0, EM_TEXTURE_BC3=1 };
enum { EM_TEXTURE_LINEAR=0, EM_TEXTURE_SWIZZLED=1 };
typedef struct {
 uint64_t identity;
 uint32_t format,layout,logical_width,logical_height,width,height,bytes;
 const uint8_t *data;
} EmTextureSource;
/* Serial render-thread callbacks. user must outlive all views and packets
 * using this host. Upload borrows source/data only for the call; it must not
 * keep that pointer. 1=ready with nonzero handle, 0=retry later with handle=0,
 * -1=permanent error with handle=0. No decompression fallback on deferral.
 * The host owns GPU synchronization/retirement; release drops a CPU lease,
 * and must defer physical deletion if GPU commands still reference it. */
typedef struct {
 void *user;
 int (*upload)(void *,const EmTextureSource *,uint64_t *handle);
 void (*release)(void *,uint64_t handle);
} EmTextureHost;
/* Optional immutable PSB backing lease. Caller keeps object valid for the
 * view lifetime. Each pending BC3 resource independently retains it until
 * upload succeeds or the final packet/view lease is cancelled. Retention
 * must keep all PSB resource bytes immutable and alive, including after the
 * view or its model-cache entry is destroyed. Callbacks are render-thread
 * serial. Without this contract the adapter continues to copy its source. */
typedef struct {
 void *object;
 void (*retain)(void *);
 void (*release)(void *);
} EmTextureSourceLease;
#ifdef __cplusplus
}
#endif
#endif
