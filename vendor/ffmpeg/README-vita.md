FFmpeg-vita
=============

A simple FFmpeg port for PS Vita, creating compatible decoders implemented by SCE hardware-accelerated codec APIs.


## New configure options

* `--enable-vita` enables vita hardware decoders, which should be selected explicitly in the decoder list.

* `--target-os=vita` setups everything needed for vita builds, like cross-compiling and flags.


## New hardware decoders

* `aac_vita` supports common sample rates and channels.

* `mp3_vita` supports common sample rates and channels.

* `h264_vita` supports resolutions up to 1080P and various output formats listed below.



## H264 direct rendering

To create VRAM-backed frame, a custom buffer allocator is necessary.
```c
struct dr_format_spec {
    enum AVPixelFormat ff_format;
    SceGxmTextureFormat sce_format;
    uint32_t alignment_pitch;
};

static const struct dr_format_spec dr_format_spec_list[] = {
    { AV_PIX_FMT_RGBA, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 16 },
    { AV_PIX_FMT_BGR565LE, SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR, 16 },
    { AV_PIX_FMT_BGR555LE, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR, 16 },
    { AV_PIX_FMT_YUV420P, SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0, 32 },
    { AV_PIX_FMT_NV12, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0, 16 },
};

static const struct dr_format_spec *get_dr_format_spec(enum AVPixelFormat fmt)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(dr_format_spec_list); i++) {
        if (dr_format_spec_list[i].ff_format == fmt)
            return &dr_format_spec_list[i];
    }
    return NULL;
}

static void vram_free(void *opaque, uint8_t *data)
{
    SceUID mb = (intptr_t) opaque;
    sceKernelFreeMemBlock(mb);
}

static bool vram_alloc(int *size, SceUID *mb, void **ptr)
{
    *size = FFALIGN(*size, 256 * 1024);
    SceUID m = sceKernelAllocMemBlock("gpu_mem",
                                      SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                      *size, NULL);
    if (m < 0)
        return false;

    void *p = NULL;
    if (sceKernelGetMemBlockBase(m, &p) != 0)
        return false;

    *mb = m;
    *ptr = p;
    return true;
}

static int get_buffer2_direct(AVCodecContext *avctx, AVFrame *pic, int flags)
{
    const struct dr_format_spec *spec = get_dr_format_spec(pic->format);
    if (!spec)
        return AVERROR_UNKNOWN;
    
    // conform to the memory layout of the decoder output
    int width = FFMAX(FFALIGN(pic->width, 16), 64);
    int height = FFMAX(FFALIGN(pic->height, 16), 64);
    int pitch = FFALIGN(width, spec->alignment_pitch);
    
    // for simplicity's sake I do not use memory pool, which is more efficient
    SceUID mb = 0;
    void *vram = NULL;
    int size = av_image_get_buffer_size(pic->format, pitch, height, 1);
    if (!vram_alloc(&size, &mb, &vram))
        return AVERROR_UNKNOWN;

    pic->buf[0] = av_buffer_create(vram, size, vram_free, (void*) mb, 0);
    av_image_fill_arrays(pic->data, pic->linesize, vram, pic->format, pitch, height, 1);
    return 0;
}
```

Set the option and the allocator if using the corresponding decoder.
```c
if (codec->id == AV_CODEC_ID_H264 && strncmp(codec->name, "h264_vita", 9) == 0) {
    av_dict_set(opts, "vita_h264_dr", "1", 0);
    ctx->get_buffer2 = get_buffer2_direct;
}
```

Bundle the hollow texture and the decoded frame together.
```c
struct dr_texture {
    vita2d_texture impl;
    AVFrame frame;
};

static struct dr_texture *dr_texture_alloc()
{
    struct dr_texture *tex = malloc(sizeof(struct dr_texture));
    memset(tex, 0, sizeof(struct dr_texture));
    av_frame_unref(&tex->frame);
    return tex;
}

static void dr_texture_free(struct dr_texture **p_tex)
{
    if (!p_tex || !(*p_tex))
        return;

    dr_texture_detach(*p_tex);
    free(*p_tex);
    *p_tex = NULL;
}
```

VRAM should be locked when being attached.
```c
static void dr_texture_attach(struct dr_texture *tex, AVFrame *frame)
{
    const struct dr_format_spec *spec = get_dr_format_spec(frame->format);
    if (!spec)
        return;

    AVBufferRef *buf = frame->buf[0];
    if (!buf)
        return;

    // the aligned size is larger than the actual one
    int width = FFMAX(FFALIGN(frame->width, 16), 64);
    int height = FFMAX(FFALIGN(frame->height, 16), 64);

    sceGxmMapMemory(buf->data, buf->size, SCE_GXM_MEMORY_ATTRIB_READ);
    sceGxmTextureInitLinear(&tex->impl.gxm_tex, buf->data, spec->sce_format, width, height, 0);
    av_frame_unref(&tex->frame);
    av_frame_move_ref(&tex->frame, frame);
}
```

Similarly, VRAM should be unlocked before being freed.
```c

static void dr_texture_detach(struct dr_texture *tex)
{
    AVBufferRef *buf = tex->frame.buf[0];
    if (!buf)
        return;

    sceGxmUnmapMemory(buf->data);
    av_frame_unref(&tex->frame);
}
```

Probably, the texture should be clipped when being shown.
```c
if (tex->frame.buf[0])
    vita2d_draw_texture_part(&tex->impl, xxx, yyy, 0, 0, tex->frame.width, tex->frame.height);

```