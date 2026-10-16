#ifndef EMOTE_GPU_H
#define EMOTE_GPU_H
#include <psp2/gxm.h>
#include <psp2/types.h>
#include <stdint.h>
typedef struct {void *data;SceUID uid;unsigned size,offset;int kind;} GpuMemory;
typedef struct {SceGxmTexture texture;GpuMemory memory;int width,height;} GpuTexture;
typedef struct {float x,y,z,w,r,g,b,a,u,v;} GpuVertex;
int gpu_init(void);
void gpu_shutdown(void);
void gpu_wait(void);
int gpu_texture(GpuTexture *t,int width,int height,const uint32_t *rgba);
int gpu_texture_bc3_linear(GpuTexture *t,int width,int height,const void *blocks,unsigned bytes);
int gpu_texture_bc3_swizzled(GpuTexture *t,int width,int height,const void *blocks,unsigned bytes);
void gpu_texture_free(GpuTexture *t);
uint32_t *gpu_ui_pixels(void);
int gpu_begin(void);
int gpu_mesh(GpuTexture *t,const GpuVertex *vertices,int count,const uint16_t *indices,int index_count);
/* Same vertex and triangle order; immutable mapped indices for steps1 or8. */
int gpu_grid(GpuTexture *t,const GpuVertex *vertices,int steps);
/* Old PSV no-filter, one-texture modes 0..5 / 0x10..0x15. Vertices must
 * use 0..1 diffuse channels, unlike the legacy demo's RGB /128 convention.
 * UINT32_MAX restores the legacy UI/demo material. Unsupported modes fail. */
int gpu_material_mode(unsigned mode);
int gpu_mask_state(unsigned read_bits,unsigned write_bit);
int gpu_mask_clear(void);
/* Original mode0 low-seven-bit counter chain. step:0=test,1=INCR,2=DECR.
 * reset preserves color and stencil bit7. No external bit7 clip is enabled. */
int gpu_hard_mask_reset(unsigned initial);
int gpu_hard_mask_step(unsigned reference,unsigned operation);
/* Non-nested original mode1 chain. begin allocates lazily; each positive
 * starts a cleared ping-pong target, negative selects subtract writer, end
 * restores display color/depth and enables sampled mask for material modes.
 * disable restores unmasked material/sampler1. All calls require gpu_begin.
 * No CPU Finish per pass; two RTs per fenced frame slot. */
int gpu_soft_mask_begin(void);
/* Optional full-resolution conservative region, after begin before any pass.
 * Half-open pixels, outward 32-pixel tile rounding; begin defaults full screen. */
int gpu_soft_mask_region(int x0,int y0,int x1,int y1);
int gpu_soft_mask_positive(void);
int gpu_soft_mask_negative(void);
int gpu_soft_mask_end(void);
int gpu_soft_mask_disable(void);
int gpu_end(void);
void gpu_capture_next(void);
#endif
