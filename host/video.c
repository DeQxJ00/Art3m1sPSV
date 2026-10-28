#include "video.h"
#include "video_direct.h"
#include "video_queue.h"
#include "video_convert.h"
#include "video_color.h"
#include "thread_perf.h"
#include "cpu_affinity.h"
#include "audio.h"
#include "media_io.h"
#include "files.h"
#include "load_timing.h"
#include "cJSON.h"
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
typedef struct VideoPlayer {
    struct VideoPlayer *next;
    int clock_active;
    char audio_id[160];
    uint64_t batch;
    int fullscreen_priority;
    HostMediaInput input;
    HostMediaInput mask_input;
    AVCodecContext *decoder;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *scaler;
    unsigned char *rgba;
    size_t rgba_charge,mask_charge;
    GLuint texture;
    int active,stream,width,height,loop,skippable,pending_frame,draining;
    int direct_mode;
    unsigned frames_uploaded;
    HostVideoQueue frame_queue;
    pthread_t decode_worker;
    int async_mode;
    int async_yuva;
    uint64_t async_clock;
    uint64_t async_report_at,async_report_upload;
    unsigned async_report_presented;
    int64_t async_last_pts,async_frame_us;
    int64_t async_duration,async_loop_base,video_local_pts;
    unsigned async_skipped_conversion;
    unsigned async_presented;
    uint64_t async_upload_us;
    uint64_t async_last_queued_wall;
    int64_t origin_pts;
    uint64_t started;
    char id[128];
    void *ogv_lease;
    const uint8_t *ogv_mask;
    size_t ogv_mask_size;
    char *pending;
    AVCodecContext *mask_decoder;
    AVFrame *mask_frame;
    AVFrame *mask_render_frame;
    AVPacket *mask_packet;
    struct SwsContext *mask_scaler;
    uint8_t *mask_pixels;
    int mask_stream,mask_draining;
    int mask_range_selected,mask_full_range;
    int64_t mask_time,mask_origin;
    struct {
    uint64_t since,decode_us,read_us,prepare_us,upload_us,present_us,max_late_us,mask_us,color_us;
    unsigned frames,presents;
} perf;
} VideoPlayer;
static VideoPlayer fullscreen_player;
static VideoPlayer *layer_players;
static __thread VideoPlayer *current_video=&fullscreen_player;
static unsigned clock_users;
static uint64_t command_batch=1;
extern void art3m1s_runtime_video_layer_ready(void *,const char *) __attribute__((weak));
static void close_player(void);
extern void art3m1s_runtime_notify_video_finished(void *,const char *);
extern int art3m1s_runtime_video_layer_is_bound(const void *,const char *) __attribute__((weak));
extern size_t host_video_reclaim_gpu_cache(void *,size_t) __attribute__((weak));
extern void host_video_prioritize_gpu(void *) __attribute__((weak));
extern int art3m1s_runtime_upload_video_layer_frame(void *,const char *,unsigned,unsigned,const unsigned char *,size_t);
extern int host_gxm_video_yuva_available(void) __attribute__((weak));
extern int host_gxm_video_yuva_upload(void *,void *,const char *,unsigned,unsigned,const uint8_t*,unsigned) __attribute__((weak));
extern void host_gxm_video_yuva_close(void *) __attribute__((weak));
extern int host_gxm_video_yuva_queue_open(void *,unsigned,unsigned,uint8_t**,unsigned) __attribute__((weak));
extern void host_gxm_video_yuva_queue_close(void *) __attribute__((weak));
// Optional host clock policy. Only the main-thread open/close lifecycle calls it;
// codec workers signal completion through the existing queue as before.
extern void host_clock_video_active(int enabled) __attribute__((weak));
static void decode_video_tick(void *runtime);
#ifdef ART3M1S_HOST_CPU3
static int cpu3_get_video_buffer(AVCodecContext *context,AVFrame *buffer,int flags){
    // Frame-threaded Theora calls this on its internal codec workers. Preserve
    // FFmpeg's allocator exactly; the host policy is thread-safe and once-only.
    // Keep decode below main (160). With two color and two alpha workers,
    // priority 159 can occupy every CPU and delay display/texture submission.
    // Still run above ordinary background loading (180/191).
    if(context->active_thread_type & FF_THREAD_FRAME)
        sceKernelChangeThreadPriority(0,161);
    host_background_thread_enter("theora-codec");
    return avcodec_default_get_buffer2(context,buffer,flags);
}
#endif
static void *video_decode_worker(void *opaque) {
    current_video=opaque;
    host_background_thread_enter("theora");
    HostThreadPerf thread_perf={0};host_thread_perf("theora",&thread_perf,0);
    while(!video_queue_done(&current_video->frame_queue)) {decode_video_tick(NULL);host_thread_perf("theora",&thread_perf,0);}
    host_thread_perf("theora",&thread_perf,1);
    return NULL;
}

static void report_video_perf(int closing){
    uint64_t now=sceKernelGetProcessTimeWide(),elapsed=now-current_video->perf.since;
    if(!current_video->perf.since||(!closing&&elapsed<5000000))return;
    if(current_video->perf.frames)av_log(NULL,AV_LOG_INFO,
        "[video-perf] frames=%u wall_ms=%llu decode_us_per_frame=%llu read_us_per_frame=%llu prepare_us_per_frame=%llu upload_us_per_frame=%llu present_us_per_call=%llu max_late_ms=%llu\n",
        current_video->perf.frames,(unsigned long long)(elapsed/1000),
        (unsigned long long)(current_video->perf.decode_us/current_video->perf.frames),(unsigned long long)(current_video->perf.read_us/current_video->perf.frames),
        (unsigned long long)(current_video->perf.prepare_us/current_video->perf.frames),(unsigned long long)(current_video->perf.upload_us/current_video->perf.frames),
        (unsigned long long)(current_video->perf.presents?current_video->perf.present_us/current_video->perf.presents:0),(unsigned long long)(current_video->perf.max_late_us/1000));
    if(current_video->perf.frames) av_log(NULL,AV_LOG_INFO,"[video-perf] mask_us_per_frame=%llu (included in prepare)\n",(unsigned long long)(current_video->perf.mask_us/current_video->perf.frames));
    if(current_video->perf.frames) av_log(NULL,AV_LOG_INFO,"[video-perf] color_us_per_frame=%llu (included in prepare)\n",(unsigned long long)(current_video->perf.color_us/current_video->perf.frames));
    if(current_video->perf.frames&&current_video->async_mode)av_log(NULL,AV_LOG_INFO,"[video-perf] async=1 frames_metric=producer_queue_commits; planar queue reservation wait included in prepare, main uploads in video-consumer\n");
    if(current_video->perf.frames&&(current_video->input.cached||current_video->mask_input.cached))av_log(NULL,AV_LOG_INFO,
        "[video-loop-cache] color_bytes=%u mask_bytes=%u color_hits=%llu mask_hits=%llu color_disk_reads=%llu mask_disk_reads=%llu; lifetime counters\n",
        (unsigned)(current_video->input.cached?current_video->input.size:0),(unsigned)(current_video->mask_input.cached?current_video->mask_input.size:0),
        (unsigned long long)current_video->input.cache_reads,(unsigned long long)current_video->mask_input.cache_reads,
        (unsigned long long)current_video->input.read_calls,(unsigned long long)current_video->mask_input.read_calls);
    memset(&current_video->perf,0,sizeof(current_video->perf));current_video->perf.since=closing?0:now;
}
/* Optional for portable host probes; production core owns the shared quota. */
extern void *art3m1s_ogv_cache_acquire(const char*,const uint8_t**,size_t*,const uint8_t**,size_t*) __attribute__((weak));
extern void art3m1s_ogv_cache_release(void*) __attribute__((weak));
static int video_fast_conversion_ready(void) {
    static int checked=-1;
    if(checked>=0)return checked;
    // CPU-only check on the actual device/library. No screen readback and no
    // overlay restrictions. Fail closed to the established swscale path.
    enum {W=32,H=8,N=W*H};
    _Alignas(32) uint8_t yp[N+64],up[N+64],vp[N+64],expected[4*N+64],actual[4*N+64];
    for(int i=0;i<N+64;i++){yp[i]=16+(i*37)%220;up[i]=16+((i/2)*53)%225;vp[i]=16+((i/2)*71)%225;}
    const uint8_t *in[]={yp,up,vp};int is[]={W,W,W},os[]={W*4};uint8_t *out[]={expected};
    struct SwsContext *check=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!check){checked=0;return 0;}
    int ok=sws_scale(check,in,is,0,H,out,os)==H,max_rgb=0,max_alpha=0,max_q6=0;
    sws_freeContext(check);
    for(int y=0;y<H;y++)host_video_yuv444_row(yp+y*W,up+y*W,vp+y*W,NULL,actual+4*y*W,W);
    for(int i=0;i<4*N;i++){int d=abs(actual[i]-expected[i]);if(d>max_rgb)max_rgb=d;}
    for(int y=0;y<H;y++)host_video_yuv444_q6_row(yp+y*W,up+y*W,vp+y*W,NULL,actual+4*y*W,W);
    for(int i=0;i<4*N;i++){int d=abs(actual[i]-expected[i]);if(d>max_q6)max_q6=d;}
    for(int i=0;i<N;i++)yp[i]=(uint8_t)i;
    check=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_GRAY8,SWS_BILINEAR,NULL,NULL,NULL);
    if(!check){checked=0;return 0;}
    os[0]=W;ok&=sws_scale(check,in,is,0,H,out,os)==H;sws_freeContext(check);
    for(int y=0;y<H;y++)host_video_gray_row(yp+y*W,actual+y*W,W);
    for(int i=0;i<N;i++){int d=abs(actual[i]-expected[i]);if(d>max_alpha)max_alpha=d;}
    checked=ok&&max_rgb<=1&&max_alpha==0;
    if(checked&&max_q6<=1)checked=2;
    av_log(NULL,AV_LOG_INFO,"[video-convert-check] enabled=%d rgb_max=%d alpha_max=%d q6=%d q6_max=%d; CPU-only BT601 limited oracle\n",checked!=0,max_rgb,max_alpha,checked==2,max_q6);
    return checked;
}
static int video_full_conversion_ready(void){
    static int checked=-1;if(checked>=0)return checked;
    enum {W=32,H=8,N=W*H};
    _Alignas(32) uint8_t y[N+64],u[N+64],v[N+64],reference[4*N+64],actual[4*N+64];
    for(int i=0;i<N+64;i++){y[i]=i;u[i]=i*37;v[i]=i*53;}
    struct SwsContext *s=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
    checked=0;if(!s)return 0;
    if(host_video_configure_scaler(s,1,SWS_CS_ITU601)<0){sws_freeContext(s);return 0;}
    const uint8_t* src[]={y,u,v};int stride[]={W,W,W},ds[]={W*4};uint8_t* dst[]={reference};
    memset(reference,0xcc,sizeof(reference));sws_scale(s,src,stride,0,H,dst,ds);sws_freeContext(s);
    for(int r=0;r<H;r++)host_video_yuv444_full_row(y+r*W,u+r*W,v+r*W,NULL,actual+r*W*4,W);
    int maximum=0;for(int i=0;i<N*4;i++){int d=abs((int)actual[i]-reference[i]);if(d>maximum)maximum=d;}
    checked=maximum<=2;av_log(NULL,AV_LOG_INFO,"[video-full-convert-check] enabled=%d max=%d\n",checked,maximum);return checked;
}
static int video_subsample_conversion_ready(enum AVPixelFormat format){
    static int checked[2]={-1,-1};
    const int sh=format==AV_PIX_FMT_YUV420P?1:0;
    if(format!=AV_PIX_FMT_YUV420P&&format!=AV_PIX_FMT_YUV422P)return 0;
    if(checked[sh]>=0)return checked[sh];
    checked[sh]=0;
    enum {W=32,H=16,N=W*H};
    _Alignas(32) uint8_t yp[N+64],up[N+64],vp[N+64],u[N],v[N],expected[4*N+64],actual[4*N+64];
    for(int i=0;i<N+64;i++){yp[i]=16+(i*37)%220;up[i]=16+(i*53)%225;vp[i]=16+(i*71)%225;}
    const uint8_t *in[4]={yp,up,vp,NULL};
    int is[4]={W,W/2,W/2,0},os[4]={W*4,0,0,0};uint8_t *out[4]={expected,NULL,NULL,NULL};
    for(int y=0;y<H;y++){
        host_video_chroma2_row(up+(y>>sh)*(W/2),u+y*W,W);
        host_video_chroma2_row(vp+(y>>sh)*(W/2),v+y*W,W);
        host_video_yuv444_row(yp+y*W,u+y*W,v+y*W,NULL,actual+4*y*W,W);
    }
    // An unwritten byte must fail even when its previous stack value happens
    // to match. The native ARM unscaled RGB wrappers return 0 after writing
    // the full image (vendor/ffmpeg/libswscale/arm/swscale_unscaled.c).
    for(int i=0;i<4*N;i++)expected[i]=actual[i]^0x80;
    struct SwsContext *check=sws_getContext(W,H,format,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!check)return 0;
    int rgb_rows=sws_scale(check,in,is,0,H,out,os),max_rgb=0,max_alpha=0;
    int ok=rgb_rows==H||rgb_rows==0;
    sws_freeContext(check);
    for(int i=0;i<4*N;i++){int d=abs(actual[i]-expected[i]);if(d>max_rgb)max_rgb=d;}
    for(int i=0;i<N;i++)yp[i]=(uint8_t)i;
    for(int y=0;y<H;y++)host_video_gray_row(yp+y*W,actual+y*W,W);
    for(int i=0;i<N;i++)expected[i]=actual[i]^0x80;
    check=sws_getContext(W,H,format,W,H,AV_PIX_FMT_GRAY8,SWS_BILINEAR,NULL,NULL,NULL);
    if(!check)return 0;
    os[0]=W;int gray_rows=sws_scale(check,in,is,0,H,out,os);sws_freeContext(check);
    ok&=gray_rows==H;
    for(int i=0;i<N;i++){int d=abs(actual[i]-expected[i]);if(d>max_alpha)max_alpha=d;}
    // swscale's subsampled integer RGB kernel differs from its 444 kernel by
    // up to 3/255; alpha must remain exact. Fail closed on this device/library.
    checked[sh]=ok&&max_rgb<=3&&max_alpha==0;
    av_log(NULL,AV_LOG_INFO,"[video-subsample-check] format=%d enabled=%d rgb_max=%d alpha_max=%d rgb_rows=%d gray_rows=%d; native swscale oracle\n",format,checked[sh],max_rgb,max_alpha,rgb_rows,gray_rows);
    return checked[sh];
}
static int video_planar_supported(enum AVPixelFormat format){
    return format==AV_PIX_FMT_YUV444P||video_subsample_conversion_ready(format);
}
static void close_mask(void){
    avcodec_free_context(&current_video->mask_decoder);av_frame_free(&current_video->mask_frame);av_frame_free(&current_video->mask_render_frame);av_packet_free(&current_video->mask_packet);
    sws_freeContext(current_video->mask_scaler);current_video->mask_scaler=NULL;av_freep(&current_video->mask_pixels);host_media_resource_release(&current_video->mask_charge);host_media_input_close(&current_video->mask_input);
}
static int open_mask(const char *path){
    char companion[512];snprintf(companion,sizeof(companion),"%s",path);
    char *ext=strrchr(companion,'.');if(!ext)return 0;
    char suffix[32];snprintf(suffix,sizeof(suffix),"_m%s",ext);
    if((size_t)(ext-companion)+strlen(suffix)>=sizeof(companion))return -1;
    strcpy(ext,suffix);
    if(current_video->ogv_lease?!current_video->ogv_mask_size:host_read(companion,NULL,0,-1)<0)return 0;
    int r=host_media_input_open_cached(&current_video->mask_input,companion,current_video->ogv_mask,current_video->ogv_mask_size);if(r<0)return r;
    const AVCodec *codec=NULL;r=av_find_best_stream(current_video->mask_input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;
    current_video->mask_stream=r;current_video->mask_decoder=avcodec_alloc_context3(codec);if(!current_video->mask_decoder)return -1;
    avcodec_parameters_to_context(current_video->mask_decoder,current_video->mask_input.format->streams[r]->codecpar);current_video->mask_decoder->thread_count=1;
    // The companion contributes only Y-derived alpha; U/V reconstruction is
    // unused. The bundled VP3 decoder honors GRAY even in the minimal build.
    if(codec->id==AV_CODEC_ID_THEORA){
        current_video->mask_decoder->flags|=AV_CODEC_FLAG_GRAY;
        // Prime the next alpha frame while color conversion is running. With
        // one codec thread every alpha dependency blocks the output producer.
        if(codec->capabilities&AV_CODEC_CAP_FRAME_THREADS){
            current_video->mask_decoder->thread_count=2;current_video->mask_decoder->thread_type=FF_THREAD_FRAME;
#ifdef ART3M1S_HOST_CPU3
            current_video->mask_decoder->get_buffer2=cpu3_get_video_buffer;
#endif
        }
    }
    if((r=avcodec_open2(current_video->mask_decoder,codec,NULL))<0)return r;
    if(current_video->mask_decoder->width!=current_video->width||current_video->mask_decoder->height!=current_video->height)return -1;
    size_t mask_bytes=(size_t)((current_video->width+31)&~31)*current_video->height+64;
    host_media_resource_event(0,mask_bytes);current_video->mask_pixels=av_malloc(mask_bytes);host_media_resource_commit(&current_video->mask_charge,mask_bytes,current_video->mask_pixels!=NULL);current_video->mask_frame=av_frame_alloc();current_video->mask_render_frame=av_frame_alloc();current_video->mask_packet=av_packet_alloc();
    if(!current_video->mask_pixels||!current_video->mask_frame||!current_video->mask_render_frame||!current_video->mask_packet)return -1;
    memset(current_video->mask_pixels,0,(size_t)current_video->width*current_video->height);current_video->mask_time=-1;current_video->mask_origin=AV_NOPTS_VALUE;current_video->mask_draining=0;
    current_video->mask_range_selected=current_video->mask_full_range=0;
    sceClibPrintf("[video] paired alpha mask %s threads=%d frame_threaded=%d\n",companion,current_video->mask_decoder->thread_count,!!(current_video->mask_decoder->active_thread_type&FF_THREAD_FRAME));return 0;
}
static int mask_at_time(int64_t target,int convert){
    if(!current_video->mask_decoder)return 0;
    while(current_video->mask_time<target){
        int r=avcodec_receive_frame(current_video->mask_decoder,current_video->mask_frame);
        if(r==AVERROR_EOF)break;
        if(r==AVERROR(EAGAIN)){
            if(current_video->mask_draining)return -1;
            while((r=av_read_frame(current_video->mask_input.format,current_video->mask_packet))>=0){if(current_video->mask_packet->stream_index==current_video->mask_stream&&current_video->mask_packet->size>0)break;av_packet_unref(current_video->mask_packet);}
            if(r<0){if(r!=AVERROR_EOF)return r;current_video->mask_draining=1;r=avcodec_send_packet(current_video->mask_decoder,NULL);}
            else{r=avcodec_send_packet(current_video->mask_decoder,current_video->mask_packet);av_packet_unref(current_video->mask_packet);}
            if(r<0)return r;continue;
        }
        if(r<0)return r;
        if(!current_video->mask_range_selected){
            current_video->mask_full_range=host_video_mask_full_range(current_video->mask_frame,current_video->mask_decoder);
            current_video->mask_range_selected=1;
        }
        int64_t pts=current_video->mask_frame->best_effort_timestamp;if(pts==AV_NOPTS_VALUE)return -1;
        if(current_video->mask_origin==AV_NOPTS_VALUE)current_video->mask_origin=pts;
        current_video->mask_time=av_rescale_q(pts-current_video->mask_origin,current_video->mask_input.format->streams[current_video->mask_stream]->time_base,(AVRational){1,1000000});
        av_frame_unref(current_video->mask_render_frame);av_frame_move_ref(current_video->mask_render_frame,current_video->mask_frame);
    }
    if(!convert)return 0;
    // Decode dependencies when catching up, but convert only the selected mask.
    // A retained frame also preserves the final mask if the next receive is EOF.
    if(current_video->mask_render_frame->data[0] && video_planar_supported(current_video->mask_render_frame->format) && video_fast_conversion_ready()) {
        for(int y=0;y<current_video->height;y++)host_video_gray_range_row(
            current_video->mask_render_frame->data[0]+(ptrdiff_t)y*current_video->mask_render_frame->linesize[0],
            current_video->mask_pixels+(size_t)y*current_video->width,current_video->width,current_video->mask_full_range);
        av_frame_unref(current_video->mask_render_frame);
    } else if(current_video->mask_render_frame->data[0]) {
        current_video->mask_scaler=sws_getCachedContext(current_video->mask_scaler,current_video->width,current_video->height,current_video->mask_render_frame->format,current_video->width,current_video->height,AV_PIX_FMT_GRAY8,SWS_BILINEAR,NULL,NULL,NULL);
        if(!current_video->mask_scaler||host_video_configure_scaler(current_video->mask_scaler,current_video->mask_full_range,SWS_CS_ITU601)<0)return -1;
        uint8_t *out[]={current_video->mask_pixels};int stride[]={(current_video->width+31)&~31};
        sws_scale(current_video->mask_scaler,(const uint8_t *const *)current_video->mask_render_frame->data,current_video->mask_render_frame->linesize,0,current_video->height,out,stride);
        if(stride[0]!=current_video->width)for(int y=1;y<current_video->height;y++)
            memmove(current_video->mask_pixels+(size_t)y*current_video->width,current_video->mask_pixels+(size_t)y*stride[0],current_video->width);
        av_frame_unref(current_video->mask_render_frame);
    }
    return 0;
}
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static void close_player(void){
    if(current_video->async_mode) {
        video_queue_stop(&current_video->frame_queue);
        uint64_t join_started=host_load_clock();int join_result=pthread_join(current_video->decode_worker,NULL);
        host_load_report("video-worker-join",current_video->id,join_started,join_started,host_load_clock(),join_result);
        av_log(NULL,AV_LOG_INFO,"[video-async] queued=%u presented=%u dropped=%u skipped_conversion=%u upload_us=%llu playback_ms=%llu; producer upload metric is queue wait/copy\n",current_video->frames_uploaded,current_video->async_presented,current_video->frame_queue.dropped,current_video->async_skipped_conversion,(unsigned long long)current_video->async_upload_us,(unsigned long long)(current_video->async_clock?(sceKernelGetProcessTimeWide()-current_video->async_clock)/1000:0));
        video_queue_destroy(&current_video->frame_queue);
        current_video->async_mode=0;
    }
    free(current_video->pending);current_video->pending=NULL;
    report_video_perf(1);
    close_mask();
    if(current_video->async_yuva&&host_gxm_video_yuva_close)host_gxm_video_yuva_close(current_video);
    current_video->async_yuva=0;
    if(current_video==&fullscreen_player)host_video_direct_release_display();
    current_video->active=0;avcodec_free_context(&current_video->decoder);av_frame_free(&current_video->frame);av_packet_free(&current_video->packet);sws_freeContext(current_video->scaler);current_video->scaler=NULL;av_freep(&current_video->rgba);host_media_resource_release(&current_video->rgba_charge);
    if(current_video==&fullscreen_player)host_video_direct_close_pool();current_video->direct_mode=0;
    host_media_input_close(&current_video->input);
    if(current_video->ogv_lease&&art3m1s_ogv_cache_release)art3m1s_ogv_cache_release(current_video->ogv_lease);
    current_video->ogv_lease=NULL;current_video->ogv_mask=NULL;current_video->ogv_mask_size=0;
    host_gxm_video_delete(current_video->texture);current_video->texture=0;
    if(*current_video->audio_id){
        cJSON *audio=cJSON_CreateObject();cJSON_AddStringToObject(audio,"id",current_video->audio_id);
        char *command=cJSON_PrintUnformatted(audio);host_media_command("audio_se_stop",command);free(command);cJSON_Delete(audio);
        current_video->audio_id[0]=0;
    }
    if(current_video->clock_active){
        current_video->clock_active=0;
        if(clock_users)--clock_users;
        if(!clock_users&&host_clock_video_active)host_clock_video_active(0);
    }
    if(current_video->fullscreen_priority){
        current_video->fullscreen_priority=0;
        av_log(NULL,AV_LOG_INFO,"[video-priority] end; resume scene preparation and rendering on demand\n");
    }
}
static void finish(void *runtime){
    if(current_video->async_mode && !runtime) { video_queue_end(&current_video->frame_queue);return; }
    char finished_id[128];strcpy(finished_id,current_video->id);close_player();art3m1s_runtime_notify_video_finished(runtime,*finished_id?finished_id:NULL);
}
static int create_video_decoder(const AVCodec *codec, int hardware,int direct){
    current_video->decoder=avcodec_alloc_context3(codec);if(!current_video->decoder)return AVERROR(ENOMEM);
    int r=avcodec_parameters_to_context(current_video->decoder,current_video->input.format->streams[current_video->stream]->codecpar);
    if(r<0)return r;
    current_video->decoder->thread_count=1;
    if(!hardware && codec->id==AV_CODEC_ID_THEORA && (codec->capabilities&AV_CODEC_CAP_FRAME_THREADS)) {
        current_video->decoder->thread_count=2;
        current_video->decoder->thread_type=FF_THREAD_FRAME;
    }
    current_video->decoder->pkt_timebase=current_video->input.format->streams[current_video->stream]->time_base;
#ifdef ART3M1S_HOST_CPU3
    if(!hardware&&codec->id==AV_CODEC_ID_THEORA)current_video->decoder->get_buffer2=cpu3_get_video_buffer;
#endif
    // This Vita decoder reads pix_fmt during init (it does not call get_format).
    if(hardware)current_video->decoder->pix_fmt=AV_PIX_FMT_RGBA;
    if(direct)host_video_direct_configure(current_video->decoder);
    uint64_t codec_started=host_load_clock();
    r=avcodec_open2(current_video->decoder,codec,NULL);
    host_load_report("video-codec-open",current_video->id,codec_started,codec_started,host_load_clock(),r);return r;
}
static int prime_hardware_frame(void){
    // Hardware allocation is postponed until the first packet. Opening the codec
    // alone cannot prove that it works; decode before starting the audio clock.
    for(int count=0;count<512;count++){
        int r=avcodec_receive_frame(current_video->decoder,current_video->frame);
        if(r!=AVERROR(EAGAIN))return r;
        while((r=av_read_frame(current_video->input.format,current_video->packet))>=0){
            if(current_video->packet->stream_index==current_video->stream&&current_video->packet->size>0)break;
            av_packet_unref(current_video->packet);
        }
        if(r<0)return r;
        r=avcodec_send_packet(current_video->decoder,current_video->packet);av_packet_unref(current_video->packet);
        if(r<0)return r;
    }
    return AVERROR_INVALIDDATA;
}
static int open_video(cJSON *j,void *runtime){
    close_player();const char *name=str(j,"id");snprintf(current_video->id,sizeof(current_video->id),"%s",name?name:"");
    const char *path=str(j,"resolved_file");if(!path)path=str(j,"file");if(!path)return -1;
    char mapped[512];snprintf(mapped,sizeof(mapped),"%s",path);
    // Converted Vita assets retain script references to .dat; prefer their MP4 copy.
    char *ext=strrchr(mapped,'.');if(ext && (!strcmp(ext,".dat")||!strcmp(ext,".ogv"))){
        snprintf(ext,sizeof(mapped)-(size_t)(ext-mapped),".mp4");
        if(host_read(mapped,NULL,0,-1)>=0)path=mapped;
    }
    const uint8_t *cached_color=NULL;size_t cached_size=0;
    const char *chosen_ext=strrchr(path,'.');
    if(chosen_ext&&!strcasecmp(chosen_ext,".ogv")&&art3m1s_ogv_cache_acquire&&art3m1s_ogv_cache_release)
        current_video->ogv_lease=art3m1s_ogv_cache_acquire(path,&cached_color,&cached_size,&current_video->ogv_mask,&current_video->ogv_mask_size);
    int r=host_media_input_open_cached(&current_video->input,path,cached_color,cached_size);if(r<0)return r;
    const AVCodec *codec=NULL;r=av_find_best_stream(current_video->input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;
    if(!*current_video->id&&host_video_prioritize_gpu){host_video_prioritize_gpu(runtime);current_video->fullscreen_priority=1;}
    if(codec->id==AV_CODEC_ID_THEORA){
        current_video->clock_active=1;
        if(clock_users++==0&&host_clock_video_active)host_clock_video_active(1);
    }
    current_video->stream=r;current_video->frame=av_frame_alloc();current_video->packet=av_packet_alloc();if(!current_video->frame||!current_video->packet)return AVERROR(ENOMEM);
    // Hardware stays opt-in for builds; full-screen H.264 first tries NV12.
    // Layer/alpha videos retain their existing RGBA compositing path.
    const AVCodec *hardware=NULL;
#ifdef ART3M1S_EXPERIMENTAL_VITA_HW
    hardware=current_video==&fullscreen_player&&current_video->input.format->streams[current_video->stream]->codecpar->codec_id==AV_CODEC_ID_H264?avcodec_find_decoder_by_name("h264_vita"):NULL;
#endif
    current_video->pending_frame=0;
    if(hardware){
        int cache_retries=0;
        for(int attempt=*current_video->id?1:0;attempt<2;attempt++){
            int try_direct=attempt==0;
            av_log(NULL,AV_LOG_INFO,"[video] hardware open %s output=%s\n",path,try_direct?"NV12-direct":"RGBA");
            r=create_video_decoder(hardware,1,try_direct);
            av_log(NULL,AV_LOG_INFO,"[video] hardware codec open returned %d; priming first frame\n",r);
            if(r>=0)r=prime_hardware_frame();
            if(r>=0&&try_direct)r=host_video_direct_present(current_video->frame);
            if(r>=0){codec=hardware;current_video->pending_frame=1;current_video->direct_mode=try_direct;av_log(NULL,AV_LOG_INFO,"[video] h264_vita first-frame OK format=%d output=%s\n",current_video->frame->format,current_video->direct_mode?"NV12-direct":"RGBA");break;}
            av_log(NULL,AV_LOG_WARNING,"[video] h264_vita %s first-frame failed %d\n",try_direct?"NV12-direct":"RGBA",r);
            if(current_video==&fullscreen_player)host_video_direct_release_display();
            av_frame_unref(current_video->frame);av_packet_unref(current_video->packet);avcodec_free_context(&current_video->decoder);
            if(current_video==&fullscreen_player)host_video_direct_close_pool();
            int retry_same=0;
            // A single 16 MiB estimate can include textures backed by main RAM,
            // leaving too little contiguous CDRAM for the codec. Try the rest
            // of the 32 MiB idle GPU allowance once, never reclaim active images.
            if(cache_retries<2&&host_video_reclaim_gpu_cache){
                ++cache_retries;
                SceKernelFreeMemorySizeInfo before={0},after={0};before.size=sizeof(before);after.size=sizeof(after);
                int before_result=sceKernelGetFreeMemorySize(&before);
                size_t released=host_video_reclaim_gpu_cache(runtime,16*1024*1024);
                int after_result=sceKernelGetFreeMemorySize(&after);
                retry_same=released>0;
                av_log(NULL,AV_LOG_INFO,"[video-memory-retry] round=%d/2 gpu_est_released=%u cdram_free_before=%d cdram_free_after=%d query_before=%d query_after=%d retry_same=%d\n",
                    cache_retries,(unsigned)released,before.size_cdram,after.size_cdram,before_result,after_result,retry_same);
                if(!released)cache_retries=2; // No eligible textures: do not retry an empty reclaim.
            }
            av_log(NULL,AV_LOG_INFO,"[video] retry %s\n",retry_same?(try_direct?"NV12-direct after cache reclaim":"hardware RGBA after cache reclaim"):(try_direct?"hardware RGBA":"software"));
            // Reopen instead of relying on demuxer seek support for archive AVIO.
            host_media_input_close(&current_video->input);
            if((r=host_media_input_open(&current_video->input,path))<0)return r;
            r=av_find_best_stream(current_video->input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;current_video->stream=r;
            if(retry_same)--attempt; // At most two extra attempts across both hardware modes.
        }
    }
    if(!current_video->decoder && (r=create_video_decoder(codec,0,0))<0)return r;
    current_video->width=current_video->decoder->width;current_video->height=current_video->decoder->height;
    if(current_video->width<1||current_video->height<1||current_video->width>1920||current_video->height>1088)return -1;
    // swscale's NEON stores require aligned output, which newlib malloc does
    // not guarantee. Reserve aligned row pitch too, including nonstandard widths.
    if(!current_video->direct_mode){
        size_t rgba_bytes=(size_t)((current_video->width*4+31)&~31)*current_video->height+64;
        host_media_resource_event(0,rgba_bytes);current_video->rgba=av_malloc(rgba_bytes);host_media_resource_commit(&current_video->rgba_charge,rgba_bytes,current_video->rgba!=NULL);if(!current_video->rgba)return -1;
        av_log(NULL,AV_LOG_INFO,"[video] RGBA output=%p pitch=%d alignment_mod16=%u\n",current_video->rgba,(current_video->width*4+31)&~31,(unsigned)((uintptr_t)current_video->rgba&15));
    }
    current_video->loop=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"loop"));current_video->skippable=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"skippable"));
    if(*current_video->id && (r=open_mask(path))<0)return r;
    if(!art3m1s_ogv_cache_acquire&&current_video->loop&&*current_video->id&&codec->id==AV_CODEC_ID_THEORA&&
       av_find_best_stream(current_video->input.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)<0){
        // Small looping effects otherwise reread the same compressed bytes on
        // every loop. Share a 4 MiB cap across color and alpha; free on close.
        size_t budget=4u*1024u*1024u;
        budget-=host_media_input_preload(&current_video->input,budget);
        host_media_input_preload(&current_video->mask_input,budget);
    }
    current_video->draining=0;current_video->frames_uploaded=0;current_video->origin_pts=AV_NOPTS_VALUE;current_video->async_loop_base=0;current_video->video_local_pts=0;current_video->started=sceKernelGetProcessTimeWide();current_video->active=1;
    memset(&current_video->perf,0,sizeof(current_video->perf));current_video->perf.since=current_video->started;
    if(av_find_best_stream(current_video->input.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)>=0){
        snprintf(current_video->audio_id,sizeof(current_video->audio_id),"__video_audio%s%s",*current_video->id?"/":"",current_video->id);
        cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"id",current_video->audio_id);cJSON_AddStringToObject(a,"file",path);cJSON_AddStringToObject(a,"resolved_file",path);cJSON_AddBoolToObject(a,"loop",current_video->loop);char *json=cJSON_PrintUnformatted(a);host_media_command("audio_se_play",json);free(json);cJSON_Delete(a);
    }
    av_log(NULL,AV_LOG_INFO,"[video] playing %s %dx%d decoder=%s\n",path,current_video->width,current_video->height,codec->name);return 0;
}
static void try_loop_cache(void){
    if(current_video->ogv_lease||current_video->input.cached||!art3m1s_ogv_cache_acquire||!art3m1s_ogv_cache_release)return;
    const char *ext=strrchr(current_video->input.path,'.');if(!ext||strcasecmp(ext,".ogv"))return;
    const uint8_t *color=NULL,*mask=NULL;size_t color_size=0,mask_size=0;
    void *lease=art3m1s_ogv_cache_acquire(current_video->input.path,&color,&color_size,&mask,&mask_size);if(!lease)return;
    if(color_size!=(uint64_t)current_video->input.size||(current_video->mask_decoder&&(mask_size!=(uint64_t)current_video->mask_input.size||current_video->mask_input.cached))){art3m1s_ogv_cache_release(lease);return;}
    host_media_input_attach_cache(&current_video->input,color,color_size);
    if(current_video->mask_decoder)host_media_input_attach_cache(&current_video->mask_input,mask,mask_size);
    current_video->ogv_lease=lease;current_video->ogv_mask=mask;current_video->ogv_mask_size=mask_size;
    av_log(NULL,AV_LOG_INFO,"[ogv-cache] attached at loop boundary path=%s color=%u mask=%u\n",current_video->input.path,(unsigned)color_size,(unsigned)mask_size);
}
static void decode_video_tick(void *runtime){
    if(!current_video->active)return;
    // Keep one decoded frame queued and schedule it by its stream timestamp.
    for(int steps=0;steps<64;steps++){
        if(!current_video->pending_frame){
            uint64_t work_start=sceKernelGetProcessTimeWide();
            int r=avcodec_receive_frame(current_video->decoder,current_video->frame);
            current_video->perf.decode_us+=sceKernelGetProcessTimeWide()-work_start;
            if(r==AVERROR_EOF){
                if(current_video->loop)try_loop_cache();
                if(current_video->loop && av_seek_frame(current_video->input.format,current_video->stream,0,AVSEEK_FLAG_BACKWARD)>=0){
                    if(current_video->mask_decoder){if(av_seek_frame(current_video->mask_input.format,current_video->mask_stream,0,AVSEEK_FLAG_BACKWARD)<0){finish(runtime);return;}avcodec_flush_buffers(current_video->mask_decoder);current_video->mask_time=-1;current_video->mask_origin=AV_NOPTS_VALUE;current_video->mask_draining=0;}
                    avcodec_flush_buffers(current_video->decoder);current_video->draining=0;current_video->origin_pts=AV_NOPTS_VALUE;
                    if(current_video->async_mode)current_video->async_loop_base+=current_video->video_local_pts+current_video->async_frame_us;
                    else current_video->started=sceKernelGetProcessTimeWide();
                    current_video->video_local_pts=0;continue;}
                finish(runtime);return;
            }
            if(r==AVERROR(EAGAIN)){
                if(current_video->draining){finish(runtime);return;}
                work_start=sceKernelGetProcessTimeWide();
                // Some Ogg demuxers expose empty Theora repeat packets. Keep
                // displaying the previous frame; sending an empty packet to
                // libavcodec would drain the decoder as if EOF were reached.
                while((r=av_read_frame(current_video->input.format,current_video->packet))>=0){if(current_video->packet->stream_index==current_video->stream&&current_video->packet->size>0)break;av_packet_unref(current_video->packet);}
                current_video->perf.read_us+=sceKernelGetProcessTimeWide()-work_start;
                work_start=sceKernelGetProcessTimeWide();
                if(r<0){current_video->draining=1;r=avcodec_send_packet(current_video->decoder,NULL);}else{r=avcodec_send_packet(current_video->decoder,current_video->packet);av_packet_unref(current_video->packet);}
                current_video->perf.decode_us+=sceKernelGetProcessTimeWide()-work_start;
                if(r<0){sceClibPrintf("[video] decode failed %d\n",r);finish(runtime);return;}continue;
            }
            if(r<0){finish(runtime);return;}current_video->pending_frame=1;
        }
        if(current_video->frame->width!=current_video->width||current_video->frame->height!=current_video->height){sceClibPrintf("[video] unsupported dimension change %dx%d -> %dx%d\n",current_video->width,current_video->height,current_video->frame->width,current_video->frame->height);finish(runtime);return;}
        int64_t pts=current_video->frame->best_effort_timestamp;if(pts==AV_NOPTS_VALUE)pts=current_video->frame->pts;if(pts==AV_NOPTS_VALUE)pts=0;if(current_video->origin_pts==AV_NOPTS_VALUE)current_video->origin_pts=pts;
        int64_t local_due=av_rescale_q(pts-current_video->origin_pts,current_video->input.format->streams[current_video->stream]->time_base,(AVRational){1,1000000});
        current_video->video_local_pts=local_due;
        int64_t due=local_due+(current_video->async_mode?current_video->async_loop_base:0);
        // Catch up only for a bounded interval. A decoder slower than realtime
        // must still publish fresh pictures instead of discarding forever.
        // The half-frame margin also covers rounding at the final timestamp.
        if(current_video->async_mode && current_video->async_duration>0 && local_due+current_video->async_frame_us+current_video->async_frame_us/2<current_video->async_duration &&
           due+current_video->async_frame_us<video_queue_clock(&current_video->frame_queue) && current_video->async_last_queued_wall &&
           sceKernelGetProcessTimeWide()-current_video->async_last_queued_wall<(uint64_t)current_video->async_frame_us) {
            // Keep both predictive streams advancing together. Deferring the
            // mask until the next displayed color frame creates a catch-up
            // burst, during which color falls behind again. No pixel conversion
            // or upload is needed for this obsolete pair.
            uint64_t mask_start=sceKernelGetProcessTimeWide();
            if(mask_at_time(local_due,0)<0){finish(runtime);return;}
            uint64_t mask_elapsed=sceKernelGetProcessTimeWide()-mask_start;
            current_video->perf.mask_us+=mask_elapsed;current_video->perf.prepare_us+=mask_elapsed;
            // Never skip codec dependencies or the known final frame. Only
            // discard color conversion/masking/upload for obsolete pictures.
            av_frame_unref(current_video->frame);current_video->pending_frame=0;current_video->async_skipped_conversion++;continue;
        }
        if(!current_video->async_mode && due>(int64_t)(sceKernelGetProcessTimeWide()-current_video->started))return;
        uint64_t prepare_start=sceKernelGetProcessTimeWide();
        int64_t late=(int64_t)(prepare_start-current_video->started)-due;
        if(late>0&&(uint64_t)late>current_video->perf.max_late_us)current_video->perf.max_late_us=late;
        if(current_video->direct_mode){
            int r=host_video_direct_present(current_video->frame);
            if(r<0){av_log(NULL,AV_LOG_ERROR,"[video-direct] present failed %d\n",r);finish(runtime);return;}
            if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video-direct] first frame bound; no RGBA conversion/upload\n");
            current_video->perf.upload_us+=sceKernelGetProcessTimeWide()-prepare_start;current_video->perf.frames++;report_video_perf(0);
            current_video->frames_uploaded++;av_frame_unref(current_video->frame);current_video->pending_frame=0;return;
        }
        const uint8_t *upload_pixels=current_video->rgba;
        uint64_t mask_start=sceKernelGetProcessTimeWide();
        int matrix=host_video_colour_matrix(current_video->frame,current_video->decoder);
        int planar=matrix==SWS_CS_ITU601&&current_video->async_mode&&current_video->async_yuva&&video_planar_supported(current_video->frame->format);
        if(mask_at_time(local_due,!planar)<0){sceClibPrintf("[video] alpha mask decode failed\n");finish(runtime);return;}
        if(planar&&current_video->mask_decoder&&(!current_video->mask_render_frame->data[0]||!video_planar_supported(current_video->mask_render_frame->format))){
            planar=0;if(mask_at_time(local_due,1)<0){finish(runtime);return;}
        }
        current_video->perf.mask_us+=sceKernelGetProcessTimeWide()-mask_start;
        uint64_t color_start=sceKernelGetProcessTimeWide();
        if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame convert format=%d stride=%d\n",current_video->frame->format,current_video->frame->linesize[0]);
        int full=host_video_colour_full_range(current_video->frame,current_video->decoder,current_video->mask_decoder&&current_video->mask_full_range);
        unsigned ranges=(full?1u:0u)|(current_video->mask_decoder&&current_video->mask_full_range?2u:0u);
        if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video-colour] colour_full=%d mask_full=%d matrix=%d metadata=%d paired=%d; metadata first, untagged paired Theora uses mask endpoints\n",full,current_video->mask_full_range,matrix,host_video_range_metadata(current_video->frame,current_video->decoder),current_video->mask_decoder!=NULL);
        int fast444=matrix==SWS_CS_ITU601&&current_video->frame->format==AV_PIX_FMT_YUV444P&&(full?video_full_conversion_ready():video_fast_conversion_ready());
        if(planar){
            uint8_t *planes=video_queue_write_begin(&current_video->frame_queue);if(!planes)return;
            size_t plane=(size_t)current_video->width*current_video->height;
            for(int c=0;c<3;c++)for(int y=0;y<current_video->height;y++){
                const int subsampled=c&&current_video->frame->format!=AV_PIX_FMT_YUV444P;
                const int row=subsampled&&current_video->frame->format==AV_PIX_FMT_YUV420P?y/2:y;
                const uint8_t *src=current_video->frame->data[c]+(ptrdiff_t)row*current_video->frame->linesize[c];
                uint8_t *dst=planes+plane*c+(size_t)y*current_video->width;
                if(subsampled)host_video_chroma2_row(src,dst,current_video->width);else memcpy(dst,src,current_video->width);
            }
            if(current_video->mask_decoder)for(int y=0;y<current_video->height;y++)
                memcpy(planes+plane*3+(size_t)y*current_video->width,current_video->mask_render_frame->data[0]+(ptrdiff_t)y*current_video->mask_render_frame->linesize[0],current_video->width);
            else memset(planes+plane*3,235,plane);
            if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video-yuva] source_format=%d packed full-size YUV and raw mask Y into reserved queue slot; no CPU color/alpha conversion\n",current_video->frame->format);
        }else if(fast444){
            void (*convert_row)(const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*,uint8_t*,int)=
                full?host_video_yuv444_full_row:video_fast_conversion_ready()==2?host_video_yuv444_q6_row:host_video_yuv444_row;
            for(int y=0;y<current_video->height;y++)convert_row(
                current_video->frame->data[0]+(ptrdiff_t)y*current_video->frame->linesize[0],
                current_video->frame->data[1]+(ptrdiff_t)y*current_video->frame->linesize[1],
                current_video->frame->data[2]+(ptrdiff_t)y*current_video->frame->linesize[2],
                current_video->mask_decoder?current_video->mask_pixels+(size_t)y*current_video->width:NULL,current_video->rgba+(size_t)y*current_video->width*4,current_video->width);
            if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] YUV444 BT601 range-aware fast conversion; paired alpha fused; neon=%d\n",
#ifdef __ARM_NEON
                1
#else
                0
#endif
            );
        }else if(current_video->frame->format==AV_PIX_FMT_RGBA&&current_video->frame->linesize[0]==current_video->width*4&&!current_video->mask_decoder){
            // The frame remains alive until the synchronous texture upload ends.
            // Packed RGBA needs no second CPU copy when there is no alpha mask.
            upload_pixels=current_video->frame->data[0];
        }else if(current_video->frame->format==AV_PIX_FMT_RGBA){
            for(int y=0;y<current_video->height;y++)memcpy(current_video->rgba+(size_t)y*current_video->width*4,current_video->frame->data[0]+(ptrdiff_t)y*current_video->frame->linesize[0],(size_t)current_video->width*4);
        }else{
            current_video->scaler=sws_getCachedContext(current_video->scaler,current_video->width,current_video->height,current_video->frame->format,current_video->width,current_video->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
            if(!current_video->scaler||host_video_configure_scaler(current_video->scaler,full,matrix)<0){finish(runtime);return;}uint8_t *out[]={current_video->rgba};int stride[]={(current_video->width*4+31)&~31};
            sws_scale(current_video->scaler,(const uint8_t *const *)current_video->frame->data,current_video->frame->linesize,0,current_video->height,out,stride);
            if(stride[0]!=current_video->width*4)for(int y=1;y<current_video->height;y++)
                memmove(current_video->rgba+(size_t)y*current_video->width*4,current_video->rgba+(size_t)y*stride[0],(size_t)current_video->width*4);
        }
        if(current_video->mask_decoder&&!fast444&&!planar)
            for(size_t i=0;i<(size_t)current_video->width*current_video->height;i++)current_video->rgba[i*4+3]=current_video->mask_pixels[i];
        current_video->perf.color_us+=sceKernelGetProcessTimeWide()-color_start;
        if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame converted; upload begin\n");
        uint64_t upload_start=sceKernelGetProcessTimeWide();current_video->perf.prepare_us+=upload_start-prepare_start;
        if(current_video->async_mode) {
            if(!(planar?video_queue_write_commit(&current_video->frame_queue,due,1|(ranges<<1)):video_queue_push_kind(&current_video->frame_queue,upload_pixels,due,0)))return;
            current_video->async_last_queued_wall=sceKernelGetProcessTimeWide();
        }
        else if(*current_video->id){
            if(art3m1s_runtime_upload_video_layer_frame(runtime,current_video->id,current_video->width,current_video->height,upload_pixels,(size_t)current_video->width*current_video->height*4)<=0){finish(runtime);return;}
            if(!current_video->frames_uploaded && art3m1s_runtime_video_layer_ready)
                art3m1s_runtime_video_layer_ready(runtime,current_video->id);
        }
        else{
            current_video->texture=host_gxm_video_rgba(current_video->texture,current_video->width,current_video->height,upload_pixels);
            if(!current_video->texture){finish(runtime);return;}
        }
        if(!current_video->frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame uploaded\n");
        current_video->perf.upload_us+=sceKernelGetProcessTimeWide()-upload_start;current_video->perf.frames++;report_video_perf(0);
        current_video->frames_uploaded++;av_frame_unref(current_video->frame);current_video->pending_frame=0;return;
    }
}
static void tick_player(void *runtime){
    if(current_video->pending) {
        char *command=current_video->pending;current_video->pending=NULL;
        cJSON *j=cJSON_Parse(command);int r=j?open_video(j,runtime):-1;cJSON_Delete(j);free(command);
        if(r<0){sceClibPrintf("[video] open failed %d\n",r);finish(runtime);return;}
        // Silent Theora layers, including looping title effects. Hardware decoders and
        // audiovisual clocks retain their existing main-thread lifecycle.
        if(current_video->decoder->codec_id==AV_CODEC_ID_THEORA && *current_video->id &&
           av_find_best_stream(current_video->input.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)<0) {
            size_t bytes=(size_t)current_video->width*current_video->height*4;
            current_video->async_yuva=current_video->width%8==0&&host_gxm_video_yuva_available&&host_gxm_video_yuva_upload&&host_gxm_video_yuva_available();
            uint8_t* mapped_slots[VIDEO_QUEUE_SLOTS]={0};
            int mapped=current_video->async_yuva&&host_gxm_video_yuva_queue_open&&host_gxm_video_yuva_queue_close&&
                host_gxm_video_yuva_queue_open(current_video,current_video->width,current_video->height,mapped_slots,VIDEO_QUEUE_SLOTS);
            int queue_result=mapped?video_queue_init_storage(&current_video->frame_queue,bytes,mapped_slots):video_queue_init(&current_video->frame_queue,bytes);
            if(mapped&&queue_result<0){
                host_gxm_video_yuva_queue_close(current_video);mapped=0;queue_result=video_queue_init(&current_video->frame_queue,bytes);
            }
            if(queue_result==0) {
                current_video->async_mode=1;current_video->async_clock=0;current_video->async_last_pts=0;current_video->async_presented=0;current_video->async_upload_us=0;current_video->async_skipped_conversion=0;current_video->async_last_queued_wall=0;
                current_video->async_report_at=current_video->async_report_upload=0;current_video->async_report_presented=0;
                AVRational rate=av_guess_frame_rate(current_video->input.format,current_video->input.format->streams[current_video->stream],NULL);
                current_video->async_frame_us=rate.num>0&&rate.den>0?av_rescale_q(1,av_inv_q(rate),(AVRational){1,1000000}):33333;
                int64_t duration=current_video->input.format->streams[current_video->stream]->duration;
                current_video->async_duration=duration>0?av_rescale_q(duration,current_video->input.format->streams[current_video->stream]->time_base,(AVRational){1,1000000}):0;
                pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,1024*1024);
                int result=pthread_create(&current_video->decode_worker,&attr,video_decode_worker,current_video);pthread_attr_destroy(&attr);
                if(result) {current_video->async_mode=0;video_queue_destroy(&current_video->frame_queue);if(mapped)host_gxm_video_yuva_queue_close(current_video);}
                else av_log(NULL,AV_LOG_INFO,"[video-async] Theora color/mask worker queue=3 bytes=%u loop=%d loan=1 mapped=%d; reserved planar producer and synchronous consumer release\n",(unsigned)(3*bytes),current_video->loop,mapped);
            }
        }
    }
    if(!current_video->active)return;
    if(!current_video->async_mode){decode_video_tick(runtime);return;}
    uint64_t now=sceKernelGetProcessTimeWide();
    if(!current_video->async_clock){
        // Body and accessory movies started by one script frame share an epoch.
        // Wait for all of that batch to prefill rather than starting each early.
        for(VideoPlayer *peer=layer_players;peer;peer=peer->next){
            if(peer->batch!=current_video->batch)continue;
            if(peer->pending||(peer->async_mode&&!video_queue_prefilled(&peer->frame_queue)))return;
        }
        for(VideoPlayer *peer=layer_players;peer;peer=peer->next){
            if(peer->batch==current_video->batch&&peer->async_mode&&!peer->async_clock){
                peer->async_clock=now;peer->async_report_at=now;
            }
        }
    }
    int64_t pts=0,clock=(int64_t)(now-current_video->async_clock);
    unsigned kind=0;
    const uint8_t *async_pixels=NULL;
    int result=video_queue_acquire(&current_video->frame_queue,clock,&async_pixels,&pts,&kind);
    if(result==1) {
        uint64_t upload_start=sceKernelGetProcessTimeWide();
        int uploaded=(kind&1)?host_gxm_video_yuva_upload(current_video,runtime,current_video->id,current_video->width,current_video->height,async_pixels,kind>>1):
            art3m1s_runtime_upload_video_layer_frame(runtime,current_video->id,current_video->width,current_video->height,async_pixels,(size_t)current_video->width*current_video->height*4);
        video_queue_release(&current_video->frame_queue);
        if(uploaded<=0){finish(runtime);return;}
        if(!current_video->async_presented && art3m1s_runtime_video_layer_ready)
            art3m1s_runtime_video_layer_ready(runtime,current_video->id);
        current_video->async_upload_us+=sceKernelGetProcessTimeWide()-upload_start;
        current_video->async_last_pts=pts;current_video->async_presented++;
        uint64_t done=sceKernelGetProcessTimeWide();
        if(done-current_video->async_report_at>=5000000){
            unsigned count=current_video->async_presented-current_video->async_report_presented;
            av_log(NULL,AV_LOG_INFO,"[video-consumer] frames=%u wall_ms=%llu upload_avg_us=%llu dropped_total=%u last_pts_us=%lld clock_us=%lld; completed main-thread uploads, not producer decoded count\n",
                count,(unsigned long long)((done-current_video->async_report_at)/1000),
                (unsigned long long)((current_video->async_upload_us-current_video->async_report_upload)/count),current_video->frame_queue.dropped,
                (long long)pts,(long long)clock);
            current_video->async_report_at=done;current_video->async_report_presented=current_video->async_presented;current_video->async_report_upload=current_video->async_upload_us;
        }
    } else if(result==2 && (!current_video->async_presented || clock>=current_video->async_last_pts+current_video->async_frame_us)) finish(runtime);
}
void host_video_present_idle(void){
    current_video=&fullscreen_player;
    unsigned image=current_video->direct_mode?host_video_direct_texture():current_video->texture;
    float u=1,v=1;if(current_video->direct_mode)host_video_direct_uv(&u,&v);
    if(current_video->active && !*current_video->id && image)host_gxm_video_draw(image,u,v);
}
int host_video_fullscreen_active(void){
    current_video=&fullscreen_player;
    return current_video->active&&!*current_video->id&&(current_video->direct_mode?host_video_direct_texture():current_video->texture)!=0;
}

// Each layer owns its decoder, paired alpha stream and completion lifecycle.
void host_video_command(const char *kind,const char *json){
    if(!strcmp(kind,"video_stop_all")){host_video_close();return;}
    cJSON *command=cJSON_Parse(json);
    if(!command)return;
    const char *name=str(command,"id");
    VideoPlayer *player=&fullscreen_player;
    if(name&&*name){
        VideoPlayer **entry=&layer_players;
        while(*entry&&strcmp((*entry)->id,name))entry=&(*entry)->next;
        if(!*entry){
            *entry=calloc(1,sizeof(VideoPlayer));
            if(!*entry){cJSON_Delete(command);return;}
            snprintf((*entry)->id,sizeof((*entry)->id),"%s",name);
        }
        player=*entry;
    }
    free(player->pending);player->pending=strdup(json);
    player->batch=command_batch;
    cJSON_Delete(command);
}
void host_video_close(void){
    current_video=&fullscreen_player;close_player();
    while(layer_players){
        VideoPlayer *player=layer_players;layer_players=player->next;
        current_video=player;close_player();free(player);
    }
    current_video=&fullscreen_player;
}
void host_video_skip(void *runtime){
    current_video=&fullscreen_player;
    if(current_video->active&&current_video->skippable&&!current_video->pending)finish(runtime);
}
void host_video_tick(void *runtime){
    ++command_batch;
    // Initialize shared conversion probes before any decoder worker reads them.
    static int conversion_probes_ready;
    if(!conversion_probes_ready){
        video_fast_conversion_ready();
        video_subsample_conversion_ready(AV_PIX_FMT_YUV420P);
        video_subsample_conversion_ready(AV_PIX_FMT_YUV422P);
        conversion_probes_ready=1;
    }
    current_video=&fullscreen_player;tick_player(runtime);
    VideoPlayer **entry=&layer_players;
    while(*entry){
        VideoPlayer *player=*entry;current_video=player;
        if(art3m1s_runtime_video_layer_is_bound&&!art3m1s_runtime_video_layer_is_bound(runtime,player->id))
            close_player(); // Script deleted or replaced the movie; no completion callback.
        else tick_player(runtime);
        if(!player->active&&!player->pending){*entry=player->next;close_player();free(player);}
        else entry=&player->next;
    }
    current_video=&fullscreen_player;
}
