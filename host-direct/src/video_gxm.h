#pragma once
#include <stdint.h>
#include <psp2/gxm.h>
#ifdef __cplusplus
extern "C" {
#endif
// Upload, import and delete are called between Borealis frames, never in draw.
unsigned host_gxm_video_rgba(unsigned image, int width, int height, const uint8_t* pixels);
unsigned host_gxm_video_import(SceGxmTexture* texture);
void host_gxm_video_delete(unsigned image);
void host_gxm_video_wait(void);
void host_gxm_video_draw(unsigned image, float u, float v);
#ifdef __cplusplus
}
#endif
