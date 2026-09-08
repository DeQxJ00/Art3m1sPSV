#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <psp2/audiodec.h>
#include <psp2/kernel/sysmem.h>

#include "decode.h"
#include "avcodec.h"
#include "internal.h"
#include "codec_internal.h"

#include "mpeg4audio.h"
#include "adts_parser.h"
#include "adts_header.h"
#include "mpegaudiodecheader.h"

#define VITA_DECODE_AUDIO_CTX_FLAG_POSTPONE_INIT    1
#define VITA_DECODE_AUDIO_CTX_FLAG_DECODER_READY    (1 << 1)
#define VITA_DECODE_AUDIO_CTX_FLAG_INIT_LIB         (1 << 2)
#define VITA_DECODE_AUDIO_CTX_FLAG_INIT_DECODER     (1 << 3)
#define VITA_DECODE_AUDIO_CTX_FLAG_INIT_BUFFER      (1 << 4)

#define VITA_AUDIO_MEM_BLOCK_NAME   "ffmpeg_adec"
#define VITA_AUDIO_MEM_BLOCK_TYPE   SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE
#define VITA_AUDIO_MEM_ALIGN_SIZE   (4 * 1024)

typedef struct VitaDecodeInputBuffer {
    uint8_t *data;
    int size;
} VitaDecodeInputBuffer;

typedef struct VitaDecodePolicyInitParams {
    const char *name;
    AVCodecContext *avctx;
    VitaDecodeInputBuffer *buffer;
    SceAudiodecInfo *info;
    SceAudiodecInitParam init;
} VitaDecodePolicyInitParams;

typedef struct VitaDecodePolicy {
    const char *name;
    SceAudiodecType type;
    bool (*init)(VitaDecodePolicyInitParams *params);
    void (*skip)(VitaDecodeInputBuffer *buffer);
} VitaDecodePolicy;

typedef struct VitaDecodeContextImpl {
    const VitaDecodePolicy *policy;
    unsigned int flags;
    SceAudiodecCtrl ctrl;
    SceAudiodecInfo info;
    SceUID memblock;
} VitaDecodeContextImpl;

typedef struct VitaDecodeContext {
    AVClass *clz;
    VitaDecodeContextImpl impl;
} VitaDecodeContext;

static const VitaDecodePolicy vita_dec_policy_aac;
static const VitaDecodePolicy vita_dec_policy_mp3;

static VitaDecodeContextImpl *get_ctx_impl(AVCodecContext *avctx)
{
    return &((VitaDecodeContext*) avctx->priv_data)->impl;
}

static const VitaDecodePolicy *find_decode_policy(AVCodecContext *avctx)
{
    switch (avctx->codec_id) {
    case AV_CODEC_ID_AAC:
        return &vita_dec_policy_aac;
    case AV_CODEC_ID_MP3:
        return &vita_dec_policy_mp3;
    default:
        return NULL;
    }
}

static av_cold int vita_init(AVCodecContext *avctx)
{
    VitaDecodeContextImpl *ctx = get_ctx_impl(avctx);
    const VitaDecodePolicy *policy = find_decode_policy(avctx);

    // every decoder should have a corresponding policy
    if (!policy)
        return AVERROR_UNKNOWN;

    memset(ctx, 0, sizeof(VitaDecodeContextImpl));

    ctx->policy = policy;
    ctx->ctrl.size = sizeof(SceAudiodecCtrl);
    ctx->ctrl.wordLength = SCE_AUDIODEC_WORD_LENGTH_16BITS;
    ctx->ctrl.pInfo = &ctx->info;

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    ctx->flags |= VITA_DECODE_AUDIO_CTX_FLAG_POSTPONE_INIT;
    return 0;
}

static int get_bytes_per_sample(AVCodecContext *avctx)
{
    return 2 * avctx->ch_layout.nb_channels;
}

static void do_cleanup(AVCodecContext *avctx)
{
    VitaDecodeContextImpl *ctx = get_ctx_impl(avctx);

    if (ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_INIT_DECODER)
        sceAudiodecDeleteDecoder(&ctx->ctrl);
    if (ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_INIT_LIB)
        sceAudiodecTermLibrary(ctx->policy->type);
    if (ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_INIT_BUFFER)
        sceKernelFreeMemBlock(ctx->memblock);

    memset(ctx, 0, sizeof(VitaDecodeContextImpl));
}

static av_cold int vita_close(AVCodecContext *avctx)
{
    do_cleanup(avctx);
    return 0;
}

static bool do_alloc_buffer(size_t size, SceUID *out_mb, void **out_ptr)
{
    int ret = 0;
    SceUID mb = -1;
    void *ptr = NULL;
    size_t size_aligned = FFALIGN(size, VITA_AUDIO_MEM_ALIGN_SIZE);
    SceKernelAllocMemBlockOpt opt = {0};

    opt.size = sizeof(SceKernelAllocMemBlockOpt);
    opt.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT;
    opt.alignment = SCE_AUDIODEC_ALIGNMENT_SIZE;

    mb = sceKernelAllocMemBlock(VITA_AUDIO_MEM_BLOCK_NAME, VITA_AUDIO_MEM_BLOCK_TYPE, size_aligned, &opt);
    if (mb < 0)
        goto bail;

    ret = sceKernelGetMemBlockBase(mb, &ptr);
    if (ret < 0)
        goto bail;

    *out_mb = mb;
    *out_ptr = ptr;
    return true;

bail:
    if (mb >= 0)
        sceKernelFreeMemBlock(mb);
    return false;
}

static void do_init(AVCodecContext *avctx, VitaDecodeInputBuffer *buffer)
{
    int ret = 0;
    void *ptr_buf = NULL;
    size_t buf_size = 0;
    size_t buf_input = 0;
    size_t buf_output = 0;
    VitaDecodeContextImpl *ctx = get_ctx_impl(avctx);
    VitaDecodePolicyInitParams params = {0};

    params.avctx = avctx;
    params.buffer = buffer;
    params.name = ctx->policy->name;
    params.info = &ctx->info;
    if (!ctx->policy->init(&params))
        goto bail;

    ret = sceAudiodecInitLibrary(ctx->policy->type, &params.init);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "vita_%s init: init library failed 0x%x\n", ctx->policy->name, ret);
        goto bail;
    }
    ctx->flags |= VITA_DECODE_AUDIO_CTX_FLAG_INIT_LIB;

    ret = sceAudiodecCreateDecoder(&ctx->ctrl, ctx->policy->type);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "vita_%s init: create decoder failed 0x%x\n", ctx->policy->name, ret);
        goto bail;
    }
    ctx->flags |= VITA_DECODE_AUDIO_CTX_FLAG_INIT_DECODER;

    buf_input = FFALIGN(ctx->ctrl.maxEsSize, SCE_AUDIODEC_ALIGNMENT_SIZE);
    buf_output = FFALIGN(ctx->ctrl.maxPcmSize, SCE_AUDIODEC_ALIGNMENT_SIZE);
    buf_size = buf_input + buf_output;
    if (!do_alloc_buffer(buf_size, &ctx->memblock, &ptr_buf)) {
        av_log(avctx, AV_LOG_ERROR, "vita_%s init: alloc failed\n", ctx->policy->name);
        goto bail;
    }
    ctx->ctrl.pEs = ptr_buf;
    ctx->ctrl.pPcm = (uint8_t*) ptr_buf + buf_input;
    ctx->flags |= VITA_DECODE_AUDIO_CTX_FLAG_INIT_BUFFER;

    ctx->flags |= VITA_DECODE_AUDIO_CTX_FLAG_DECODER_READY;
    return;

bail:
    do_cleanup(avctx);
}

static int vita_decode(AVCodecContext *avctx, AVFrame *frame, int *got_frame, AVPacket *avpkt)
{
    int ret = 0;
    size_t input_bytes = 0;
    VitaDecodeContextImpl *ctx = get_ctx_impl(avctx);
    VitaDecodeInputBuffer buffer = { .data = avpkt->data, .size = avpkt->size };

    if (ctx->policy->skip)
        ctx->policy->skip(&buffer);

    if (!buffer.size)
        return avpkt->size;

    if (ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_POSTPONE_INIT) {
        ctx->flags &= ~VITA_DECODE_AUDIO_CTX_FLAG_POSTPONE_INIT;
        do_init(avctx, &buffer);
    }

    // ignore any decode requests if init failed
    if (!(ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_DECODER_READY))
        return AVERROR_UNKNOWN;

    // the input may not conform to requirements of the APIs
    // so it had better be copied to our buffer
    input_bytes = FFMIN(ctx->ctrl.maxEsSize, buffer.size);
    memcpy(ctx->ctrl.pEs, buffer.data, input_bytes);

    ret = sceAudiodecDecodeNFrames(&ctx->ctrl, 1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "vita_%s decode: failed 0x%x\n", ctx->policy->name, ret);
        return AVERROR_UNKNOWN;
    }

    if (ctx->ctrl.outputPcmSize) {
        frame->nb_samples = ctx->ctrl.outputPcmSize / get_bytes_per_sample(avctx);
        ret = ff_get_buffer(avctx, frame, 0);
        if (ret < 0)
            return ret;

        *got_frame = 1;
        memcpy(frame->data[0], ctx->ctrl.pPcm, ctx->ctrl.outputPcmSize);
    }

    // consumed byte count
    return ctx->ctrl.inputEsSize;
}

static void vita_flush(AVCodecContext *avctx)
{
    int ret = 0;
    VitaDecodeContextImpl *ctx = get_ctx_impl(avctx);

    if (!(ctx->flags & VITA_DECODE_AUDIO_CTX_FLAG_DECODER_READY))
        return;

    ret = sceAudiodecClearContext(&ctx->ctrl);
    if (ret != 0)
        av_log(avctx, AV_LOG_ERROR, "vita_%s flush: failed 0x%x\n", ctx->policy->name, ret);
}

static bool do_check_header_parse(VitaDecodePolicyInitParams *params, int ret)
{
    if (ret < 0) {
        av_log(params->avctx, AV_LOG_ERROR, "vita_%s init: parse header failed 0x%x\n", params->name, ret);
        return false;
    }
    return true;
}

static bool do_set_avctx_fields(VitaDecodePolicyInitParams *params, int channels, int sample_rate) {
    if (channels != 1 && channels != 2) {
        av_log(params->avctx, AV_LOG_ERROR, "vita_%s init: invalid channels %d\n", params->name, channels);
        return false;
    }

    av_channel_layout_uninit(&params->avctx->ch_layout);
    av_channel_layout_default(&params->avctx->ch_layout, channels);
    params->avctx->sample_rate = sample_rate;
    return true;
}

static bool vita_dec_policy_init_aac(VitaDecodePolicyInitParams *params)
{
    int ret = 0;
    int has_adts = 0;
    int channels = 0;
    int sample_rate = 0;
    bool parse_done = false;

    if (params->avctx->extradata) {
        MPEG4AudioConfig cfg;
        ret = avpriv_mpeg4audio_get_config2(&cfg, 
            params->avctx->extradata, params->avctx->extradata_size,
            1, params->avctx);
        parse_done = (ret >= 0);
        has_adts = 0;
        channels = cfg.chan_config;
        sample_rate = cfg.sample_rate;        
    }

    if (!parse_done) {
        AACADTSHeaderInfo header;
        AACADTSHeaderInfo *ref = &header;
        ret = avpriv_adts_header_parse(&ref, params->buffer->data, params->buffer->size);
        if (!do_check_header_parse(params, ret))
            return false;
        has_adts = 1;
        channels = header.chan_config;
        sample_rate = header.sample_rate;
    }

    if (!do_set_avctx_fields(params, channels, sample_rate))
        return false;

    params->init.size = sizeof(params->init.aac);
    params->init.aac.totalStreams = 1;

    params->info->aac.size = sizeof(SceAudiodecInfoAac);
    params->info->aac.ch = channels;
    params->info->aac.samplingRate = sample_rate;
    params->info->aac.isAdts = has_adts;
    params->info->aac.isSbr = 1;   // cannot get it from header, set true in case we need it

    return true;
}

static bool do_get_mp3_header(VitaDecodePolicyInitParams *params, VitaDecodeInputBuffer *buffer, uint32_t *header)
{
    if (buffer->size < 4) {
        if (params)
            av_log(params->avctx, AV_LOG_ERROR, "vita_%s init: invalid header size %d\n", params->name, buffer->size);
        return false;
    } else {
        *header = AV_RB32(buffer->data);
        return true;
    }
}

static void vita_dec_policy_skip_mp3(VitaDecodeInputBuffer *buffer)
{
    uint32_t header = 0;

    // I don't know how to make these malformed data
    // just follow the implementation of mpegaudio decoder
    while (buffer->size && !*buffer->data) {
        buffer->data++;
        buffer->size--;
    }

    if (!do_get_mp3_header(NULL, buffer, &header))
        return;

    // the library cannot handle ID3 data, which should be skipped by the caller
    // however, the decoder is unlikely to receive these data from the parser or demuxer
    // since header check is not expensive, it's worth doing so for every packet
    if ((header >> 8) == (AV_RB32("TAG") >> 8))
        buffer->size = 0;
}

static SceAudiodecMpegVersion do_get_mp3_version(uint32_t header)
{
    uint32_t bits = (header >> 19) & 0x03;
    switch (bits) {
    case 0x00:
        return SCE_AUDIODEC_MP3_MPEG_VERSION_2_5;
    case 0x02:
        return SCE_AUDIODEC_MP3_MPEG_VERSION_2;
    case 0x03:
        return SCE_AUDIODEC_MP3_MPEG_VERSION_1;
    default:
        return SCE_AUDIODEC_MP3_MPEG_VERSION_RESERVED;
    }
}

static bool vita_dec_policy_init_mp3(VitaDecodePolicyInitParams *params)
{
    uint32_t header = 0;
    MPADecodeHeader mdh;

    if (!do_get_mp3_header(params, params->buffer, &header))
        return false;

    if (!do_check_header_parse(params, avpriv_mpegaudio_decode_header(&mdh, header)))
        return false;

    if (!do_set_avctx_fields(params, mdh.nb_channels, mdh.sample_rate))
        return false;

    params->init.size = sizeof(params->init.mp3);
    params->init.mp3.totalStreams = 1;

    params->info->mp3.size = sizeof(SceAudiodecInfoMp3);
    params->info->mp3.ch = mdh.nb_channels;
    params->info->mp3.version = do_get_mp3_version(header);

    return true;
}

static const VitaDecodePolicy vita_dec_policy_aac = {
    .name   = "aac",
    .type   = SCE_AUDIODEC_TYPE_AAC,
    .init   = vita_dec_policy_init_aac,
};

static const VitaDecodePolicy vita_dec_policy_mp3 = {
    .name   = "mp3",
    .type   = SCE_AUDIODEC_TYPE_MP3,
    .init   = vita_dec_policy_init_mp3,
    .skip   = vita_dec_policy_skip_mp3,
};

#define FFVITA_DEC(NAME, ID) \
    static const AVClass vita_##NAME##_dec_class = { \
        .class_name     = "vita_" AV_STRINGIFY(NAME) "_dec", \
        .item_name      = av_default_item_name, \
        .version        = LIBAVUTIL_VERSION_INT, \
    }; \
    const FFCodec ff_##NAME##_vita_decoder = { \
        .p.name             = AV_STRINGIFY(NAME) "_vita", \
        CODEC_LONG_NAME(AV_STRINGIFY(NAME) " (vita)"), \
        .p.type             = AVMEDIA_TYPE_AUDIO, \
        .p.id               = ID, \
        .priv_data_size     = sizeof(VitaDecodeContext), \
        .init               = vita_init, \
        .close              = vita_close, \
        .flush              = vita_flush, \
        FF_CODEC_DECODE_CB(vita_decode), \
        .p.sample_fmts      = (const enum AVSampleFormat[]) { \
            AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE, \
        }, \
        .p.ch_layouts       = (const AVChannelLayout[]) { \
            AV_CHANNEL_LAYOUT_MONO, AV_CHANNEL_LAYOUT_STEREO, { 0 }, \
        }, \
        .p.priv_class       = &vita_##NAME##_dec_class, \
        .p.capabilities     = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_AVOID_PROBING | AV_CODEC_CAP_HARDWARE, \
        .caps_internal      = FF_CODEC_CAP_NOT_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP, \
        .p.wrapper_name     = "vita", \
    };

FFVITA_DEC(aac, AV_CODEC_ID_AAC)
FFVITA_DEC(mp3, AV_CODEC_ID_MP3)
