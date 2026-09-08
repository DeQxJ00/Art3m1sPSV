#pragma once
#include <libavcodec/avcodec.h>
#ifdef ART3M1S_HOST_GXM
#include "video_gxm.h"
typedef unsigned GLuint;
#else
#include <vitaGL.h>
#endif

// Single-threaded video decoder only. Displayed frames remain referenced until
// GPU completion. Release the display before freeing the decoder and pool.
void host_video_direct_configure(AVCodecContext *decoder);
int host_video_direct_present(const AVFrame *frame);
GLuint host_video_direct_texture(void);
void host_video_direct_uv(float *u, float *v);
void host_video_direct_release_display(void);
void host_video_direct_close_pool(void);
