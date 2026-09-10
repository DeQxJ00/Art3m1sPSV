#include "video.h"
#include "video_direct.h"
#include "video_queue.h"
#include "thread_perf.h"
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
#ifndef ART3M1S_HOST_GXM
#include <vitaGL.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
extern void art3m1s_runtime_notify_video_finished(void *,const char *);
extern size_t host_video_reclaim_gpu_cache(void *,size_t) __attribute__((weak));
extern int art3m1s_runtime_upload_video_layer_frame(void *,const char *,unsigned,unsigned,const unsigned char *,size_t);
static HostMediaInput input;
static AVCodecContext *decoder;
static AVFrame *frame;
static AVPacket *packet;
static struct SwsContext *scaler;
static unsigned char *rgba;
static size_t rgba_charge,async_charge,mask_charge;
static GLuint texture;
static int active,stream,width,height,loop,skippable,pending_frame,draining;
static int direct_mode;
static unsigned frames_uploaded;
static HostVideoQueue frame_queue;
static pthread_t decode_worker;
static int async_mode;
static uint8_t *async_pixels;
static uint64_t async_clock;
static int64_t async_last_pts,async_frame_us;
static int64_t async_duration;
static unsigned async_skipped_conversion;
static unsigned async_presented;
static uint64_t async_upload_us;
static void decode_video_tick(void *runtime);
static void *video_decode_worker(void *unused) {
    HostThreadPerf thread_perf={0};host_thread_perf("theora",&thread_perf,0);
    while(!video_queue_done(&frame_queue)) {decode_video_tick(NULL);host_thread_perf("theora",&thread_perf,0);}
    host_thread_perf("theora",&thread_perf,1);
    return NULL;
}
static struct {
    uint64_t since,decode_us,read_us,prepare_us,upload_us,present_us,max_late_us,mask_us;
    unsigned frames,presents;
} perf;
static void report_video_perf(int closing){
    if(async_mode&&!closing)return;
    uint64_t now=sceKernelGetProcessTimeWide(),elapsed=now-perf.since;
    if(!perf.since||(!closing&&elapsed<5000000))return;
    if(perf.frames)av_log(NULL,AV_LOG_INFO,
        "[video-perf] frames=%u wall_ms=%llu decode_us_per_frame=%llu read_us_per_frame=%llu prepare_us_per_frame=%llu upload_us_per_frame=%llu present_us_per_call=%llu max_late_ms=%llu\n",
        perf.frames,(unsigned long long)(elapsed/1000),
        (unsigned long long)(perf.decode_us/perf.frames),(unsigned long long)(perf.read_us/perf.frames),
        (unsigned long long)(perf.prepare_us/perf.frames),(unsigned long long)(perf.upload_us/perf.frames),
        (unsigned long long)(perf.presents?perf.present_us/perf.presents:0),(unsigned long long)(perf.max_late_us/1000));
    if(perf.frames) av_log(NULL,AV_LOG_INFO,"[video-perf] mask_us_per_frame=%llu (included in prepare)\n",(unsigned long long)(perf.mask_us/perf.frames));
    memset(&perf,0,sizeof(perf));perf.since=closing?0:now;
}
static int64_t origin_pts=AV_NOPTS_VALUE;
static uint64_t started;
static char id[128];
static char *pending;
static HostMediaInput mask_input;
static AVCodecContext *mask_decoder;
static AVFrame *mask_frame;
static AVFrame *mask_render_frame;
static AVPacket *mask_packet;
static struct SwsContext *mask_scaler;
static uint8_t *mask_pixels;
static int mask_stream,mask_draining;
static int64_t mask_time,mask_origin;
static void close_mask(void){
    avcodec_free_context(&mask_decoder);av_frame_free(&mask_frame);av_frame_free(&mask_render_frame);av_packet_free(&mask_packet);
    sws_freeContext(mask_scaler);mask_scaler=NULL;av_freep(&mask_pixels);host_media_resource_release(&mask_charge);host_media_input_close(&mask_input);
}
static int open_mask(const char *path){
    char companion[512];snprintf(companion,sizeof(companion),"%s",path);
    char *ext=strrchr(companion,'.');if(!ext)return 0;
    char suffix[32];snprintf(suffix,sizeof(suffix),"_m%s",ext);
    if((size_t)(ext-companion)+strlen(suffix)>=sizeof(companion))return -1;
    strcpy(ext,suffix);if(host_read(companion,NULL,0,-1)<0)return 0;
    int r=host_media_input_open(&mask_input,companion);if(r<0)return r;
    const AVCodec *codec=NULL;r=av_find_best_stream(mask_input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;
    mask_stream=r;mask_decoder=avcodec_alloc_context3(codec);if(!mask_decoder)return -1;
    avcodec_parameters_to_context(mask_decoder,mask_input.format->streams[r]->codecpar);mask_decoder->thread_count=1;
    if((r=avcodec_open2(mask_decoder,codec,NULL))<0)return r;
    if(mask_decoder->width!=width||mask_decoder->height!=height)return -1;
    size_t mask_bytes=(size_t)((width+31)&~31)*height+64;
    host_media_resource_event(0,mask_bytes);mask_pixels=av_malloc(mask_bytes);host_media_resource_commit(&mask_charge,mask_bytes,mask_pixels!=NULL);mask_frame=av_frame_alloc();mask_render_frame=av_frame_alloc();mask_packet=av_packet_alloc();
    if(!mask_pixels||!mask_frame||!mask_render_frame||!mask_packet)return -1;
    memset(mask_pixels,0,(size_t)width*height);mask_time=-1;mask_origin=AV_NOPTS_VALUE;mask_draining=0;
    sceClibPrintf("[video] paired alpha mask %s\n",companion);return 0;
}
static int mask_at_time(int64_t target){
    if(!mask_decoder)return 0;
    while(mask_time<target){
        int r=avcodec_receive_frame(mask_decoder,mask_frame);
        if(r==AVERROR_EOF)break;
        if(r==AVERROR(EAGAIN)){
            if(mask_draining)return -1;
            while((r=av_read_frame(mask_input.format,mask_packet))>=0){if(mask_packet->stream_index==mask_stream)break;av_packet_unref(mask_packet);}
            if(r<0){if(r!=AVERROR_EOF)return r;mask_draining=1;r=avcodec_send_packet(mask_decoder,NULL);}
            else{r=avcodec_send_packet(mask_decoder,mask_packet);av_packet_unref(mask_packet);}
            if(r<0)return r;continue;
        }
        if(r<0)return r;
        int64_t pts=mask_frame->best_effort_timestamp;if(pts==AV_NOPTS_VALUE)return -1;
        if(mask_origin==AV_NOPTS_VALUE)mask_origin=pts;
        mask_time=av_rescale_q(pts-mask_origin,mask_input.format->streams[mask_stream]->time_base,(AVRational){1,1000000});
        av_frame_unref(mask_render_frame);av_frame_move_ref(mask_render_frame,mask_frame);
    }
    // Decode dependencies when catching up, but convert only the selected mask.
    // A retained frame also preserves the final mask if the next receive is EOF.
    if(mask_render_frame->data[0]) {
        mask_scaler=sws_getCachedContext(mask_scaler,width,height,mask_render_frame->format,width,height,AV_PIX_FMT_GRAY8,SWS_BILINEAR,NULL,NULL,NULL);
        if(!mask_scaler)return -1;
        uint8_t *out[]={mask_pixels};int stride[]={(width+31)&~31};
        sws_scale(mask_scaler,(const uint8_t *const *)mask_render_frame->data,mask_render_frame->linesize,0,height,out,stride);
        if(stride[0]!=width)for(int y=1;y<height;y++)
            memmove(mask_pixels+(size_t)y*width,mask_pixels+(size_t)y*stride[0],width);
        av_frame_unref(mask_render_frame);
    }
    for(size_t i=0;i<(size_t)width*height;i++)rgba[i*4+3]=mask_pixels[i];
    return 0;
}
static const char *str(cJSON *j,const char *key){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
void host_video_close(void){
    if(async_mode) {
        video_queue_stop(&frame_queue);
        uint64_t join_started=host_load_clock();int join_result=pthread_join(decode_worker,NULL);
        host_load_report("video-worker-join",id,join_started,join_started,host_load_clock(),join_result);
        av_log(NULL,AV_LOG_INFO,"[video-async] queued=%u presented=%u dropped=%u skipped_conversion=%u upload_us=%llu playback_ms=%llu; producer upload metric is queue wait/copy\n",frames_uploaded,async_presented,frame_queue.dropped,async_skipped_conversion,(unsigned long long)async_upload_us,(unsigned long long)(async_clock?(sceKernelGetProcessTimeWide()-async_clock)/1000:0));
        video_queue_destroy(&frame_queue);
        av_freep(&async_pixels);host_media_resource_release(&async_charge);
        async_mode=0;
    }
    free(pending);pending=NULL;
    report_video_perf(1);
    close_mask();
    host_video_direct_release_display();
    active=0;avcodec_free_context(&decoder);av_frame_free(&frame);av_packet_free(&packet);sws_freeContext(scaler);scaler=NULL;av_freep(&rgba);host_media_resource_release(&rgba_charge);
    host_video_direct_close_pool();direct_mode=0;
    host_media_input_close(&input);
#ifdef ART3M1S_HOST_GXM
    host_gxm_video_delete(texture);texture=0;
#else
    if(texture){glDeleteTextures(1,&texture);texture=0;}
#endif
    host_media_command("audio_se_stop","{\"id\":\"__video_audio\",\"fade_ms\":0}");
}
static void finish(void *runtime){
    if(async_mode && !runtime) { video_queue_end(&frame_queue);return; }
    char finished_id[128];strcpy(finished_id,id);host_video_close();art3m1s_runtime_notify_video_finished(runtime,*finished_id?finished_id:NULL);
}
void host_video_command(const char *kind,const char *json){
    free(pending);pending=NULL;
    if(!strcmp(kind,"video_stop_all")){host_video_close();return;}
    pending=strdup(json);
}
void host_video_skip(void *runtime){if(active && skippable)finish(runtime);}
static int create_video_decoder(const AVCodec *codec, int hardware,int direct){
    decoder=avcodec_alloc_context3(codec);if(!decoder)return AVERROR(ENOMEM);
    int r=avcodec_parameters_to_context(decoder,input.format->streams[stream]->codecpar);
    if(r<0)return r;
    decoder->thread_count=1;
    if(!hardware && codec->id==AV_CODEC_ID_THEORA && (codec->capabilities&AV_CODEC_CAP_FRAME_THREADS)) {
        decoder->thread_count=2;
        decoder->thread_type=FF_THREAD_FRAME;
    }
    decoder->pkt_timebase=input.format->streams[stream]->time_base;
    // This Vita decoder reads pix_fmt during init (it does not call get_format).
    if(hardware)decoder->pix_fmt=AV_PIX_FMT_RGBA;
    if(direct)host_video_direct_configure(decoder);
    uint64_t started=host_load_clock();
    r=avcodec_open2(decoder,codec,NULL);
    host_load_report("video-codec-open",id,started,started,host_load_clock(),r);return r;
}
static int prime_hardware_frame(void){
    // Hardware allocation is postponed until the first packet. Opening the codec
    // alone cannot prove that it works; decode before starting the audio clock.
    for(int count=0;count<512;count++){
        int r=avcodec_receive_frame(decoder,frame);
        if(r!=AVERROR(EAGAIN))return r;
        while((r=av_read_frame(input.format,packet))>=0){
            if(packet->stream_index==stream)break;
            av_packet_unref(packet);
        }
        if(r<0)return r;
        r=avcodec_send_packet(decoder,packet);av_packet_unref(packet);
        if(r<0)return r;
    }
    return AVERROR_INVALIDDATA;
}
static int open_video(cJSON *j,void *runtime){
    host_video_close();const char *name=str(j,"id");snprintf(id,sizeof(id),"%s",name?name:"");
    const char *path=str(j,"resolved_file");if(!path)path=str(j,"file");if(!path)return -1;
    char mapped[512];snprintf(mapped,sizeof(mapped),"%s",path);
    // Converted Vita assets retain script references to .dat; prefer their MP4 copy.
    char *ext=strrchr(mapped,'.');if(ext && (!strcmp(ext,".dat")||!strcmp(ext,".ogv"))){
        snprintf(ext,sizeof(mapped)-(size_t)(ext-mapped),".mp4");
        if(host_read(mapped,NULL,0,-1)>=0)path=mapped;
    }
    int r=host_media_input_open(&input,path);if(r<0)return r;
    const AVCodec *codec=NULL;r=av_find_best_stream(input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;
    stream=r;frame=av_frame_alloc();packet=av_packet_alloc();if(!frame||!packet)return AVERROR(ENOMEM);
    // Hardware stays opt-in for builds; full-screen H.264 first tries NV12.
    // Layer/alpha videos retain their existing RGBA compositing path.
    const AVCodec *hardware=NULL;
#ifdef ART3M1S_EXPERIMENTAL_VITA_HW
    hardware=input.format->streams[stream]->codecpar->codec_id==AV_CODEC_ID_H264?avcodec_find_decoder_by_name("h264_vita"):NULL;
#endif
    pending_frame=0;
    if(hardware){
        int cache_retry_done=0;
        for(int attempt=*id?1:0;attempt<2;attempt++){
            int try_direct=attempt==0;
            av_log(NULL,AV_LOG_INFO,"[video] hardware open %s output=%s\n",path,try_direct?"NV12-direct":"RGBA");
            r=create_video_decoder(hardware,1,try_direct);
            av_log(NULL,AV_LOG_INFO,"[video] hardware codec open returned %d; priming first frame\n",r);
            if(r>=0)r=prime_hardware_frame();
            if(r>=0&&try_direct)r=host_video_direct_present(frame);
            if(r>=0){codec=hardware;pending_frame=1;direct_mode=try_direct;av_log(NULL,AV_LOG_INFO,"[video] h264_vita first-frame OK format=%d output=%s\n",frame->format,direct_mode?"NV12-direct":"RGBA");break;}
            av_log(NULL,AV_LOG_WARNING,"[video] h264_vita %s first-frame failed %d\n",try_direct?"NV12-direct":"RGBA",r);
            host_video_direct_release_display();
            av_frame_unref(frame);av_packet_unref(packet);avcodec_free_context(&decoder);
            host_video_direct_close_pool();
            int retry_same=0;
            if(!cache_retry_done&&host_video_reclaim_gpu_cache){
                cache_retry_done=1;
                SceKernelFreeMemorySizeInfo before={0},after={0};before.size=sizeof(before);after.size=sizeof(after);
                int before_result=sceKernelGetFreeMemorySize(&before);
                size_t released=host_video_reclaim_gpu_cache(runtime,16*1024*1024);
                int after_result=sceKernelGetFreeMemorySize(&after);
                retry_same=released>0;
                av_log(NULL,AV_LOG_INFO,"[video-memory-retry] gpu_est_released=%u cdram_free_before=%d cdram_free_after=%d query_before=%d query_after=%d retry_same=%d\n",
                    (unsigned)released,before.size_cdram,after.size_cdram,before_result,after_result,retry_same);
            }
            av_log(NULL,AV_LOG_INFO,"[video] retry %s\n",retry_same?(try_direct?"NV12-direct after cache reclaim":"hardware RGBA after cache reclaim"):(try_direct?"hardware RGBA":"software"));
            // Reopen instead of relying on demuxer seek support for archive AVIO.
            host_media_input_close(&input);
            if((r=host_media_input_open(&input,path))<0)return r;
            r=av_find_best_stream(input.format,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);if(r<0)return r;stream=r;
            if(retry_same)--attempt; // At most one extra attempt for this video.
        }
    }
    if(!decoder && (r=create_video_decoder(codec,0,0))<0)return r;
    width=decoder->width;height=decoder->height;
    if(width<1||height<1||width>1920||height>1088)return -1;
    // swscale's NEON stores require aligned output, which newlib malloc does
    // not guarantee. Reserve aligned row pitch too, including nonstandard widths.
    if(!direct_mode){
        size_t rgba_bytes=(size_t)((width*4+31)&~31)*height+64;
        host_media_resource_event(0,rgba_bytes);rgba=av_malloc(rgba_bytes);host_media_resource_commit(&rgba_charge,rgba_bytes,rgba!=NULL);if(!rgba)return -1;
        av_log(NULL,AV_LOG_INFO,"[video] RGBA output=%p pitch=%d alignment_mod16=%u\n",rgba,(width*4+31)&~31,(unsigned)((uintptr_t)rgba&15));
    }
    loop=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"loop"));skippable=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"skippable"));
    if(*id && (r=open_mask(path))<0)return r;
    draining=0;frames_uploaded=0;origin_pts=AV_NOPTS_VALUE;started=sceKernelGetProcessTimeWide();active=1;
    memset(&perf,0,sizeof(perf));perf.since=started;
    if(av_find_best_stream(input.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)>=0){
        cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"id","__video_audio");cJSON_AddStringToObject(a,"file",path);cJSON_AddStringToObject(a,"resolved_file",path);cJSON_AddBoolToObject(a,"loop",loop);char *json=cJSON_PrintUnformatted(a);host_media_command("audio_se_play",json);free(json);cJSON_Delete(a);
    }
    av_log(NULL,AV_LOG_INFO,"[video] playing %s %dx%d decoder=%s\n",path,width,height,codec->name);return 0;
}
static void decode_video_tick(void *runtime){
    if(!active)return;
    // Keep one decoded frame queued and schedule it by its stream timestamp.
    for(int steps=0;steps<64;steps++){
        if(!pending_frame){
            uint64_t work_start=sceKernelGetProcessTimeWide();
            int r=avcodec_receive_frame(decoder,frame);
            perf.decode_us+=sceKernelGetProcessTimeWide()-work_start;
            if(r==AVERROR_EOF){
                if(loop && av_seek_frame(input.format,stream,0,AVSEEK_FLAG_BACKWARD)>=0){
                    if(mask_decoder){if(av_seek_frame(mask_input.format,mask_stream,0,AVSEEK_FLAG_BACKWARD)<0){finish(runtime);return;}avcodec_flush_buffers(mask_decoder);mask_time=-1;mask_origin=AV_NOPTS_VALUE;mask_draining=0;}
                    avcodec_flush_buffers(decoder);draining=0;origin_pts=AV_NOPTS_VALUE;started=sceKernelGetProcessTimeWide();continue;}
                finish(runtime);return;
            }
            if(r==AVERROR(EAGAIN)){
                if(draining){finish(runtime);return;}
                work_start=sceKernelGetProcessTimeWide();
                while((r=av_read_frame(input.format,packet))>=0){if(packet->stream_index==stream)break;av_packet_unref(packet);}
                perf.read_us+=sceKernelGetProcessTimeWide()-work_start;
                work_start=sceKernelGetProcessTimeWide();
                if(r<0){draining=1;r=avcodec_send_packet(decoder,NULL);}else{r=avcodec_send_packet(decoder,packet);av_packet_unref(packet);}
                perf.decode_us+=sceKernelGetProcessTimeWide()-work_start;
                if(r<0){sceClibPrintf("[video] decode failed %d\n",r);finish(runtime);return;}continue;
            }
            if(r<0){finish(runtime);return;}pending_frame=1;
        }
        if(frame->width!=width||frame->height!=height){sceClibPrintf("[video] unsupported dimension change %dx%d -> %dx%d\n",width,height,frame->width,frame->height);finish(runtime);return;}
        int64_t pts=frame->best_effort_timestamp;if(pts==AV_NOPTS_VALUE)pts=frame->pts;if(pts==AV_NOPTS_VALUE)pts=0;if(origin_pts==AV_NOPTS_VALUE)origin_pts=pts;
        int64_t due=av_rescale_q(pts-origin_pts,input.format->streams[stream]->time_base,(AVRational){1,1000000});
        if(async_mode && async_duration>0 && due+async_frame_us<async_duration &&
           due+async_frame_us<video_queue_clock(&frame_queue)) {
            // Never skip codec dependencies or the known final frame. Only
            // discard color conversion/masking/upload for obsolete pictures.
            av_frame_unref(frame);pending_frame=0;async_skipped_conversion++;continue;
        }
        if(!async_mode && due>(int64_t)(sceKernelGetProcessTimeWide()-started))return;
        uint64_t prepare_start=sceKernelGetProcessTimeWide();
        int64_t late=(int64_t)(prepare_start-started)-due;
        if(late>0&&(uint64_t)late>perf.max_late_us)perf.max_late_us=late;
        if(direct_mode){
            int r=host_video_direct_present(frame);
            if(r<0){av_log(NULL,AV_LOG_ERROR,"[video-direct] present failed %d\n",r);finish(runtime);return;}
            if(!frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video-direct] first frame bound; no RGBA conversion/upload\n");
            perf.upload_us+=sceKernelGetProcessTimeWide()-prepare_start;perf.frames++;report_video_perf(0);
            frames_uploaded++;av_frame_unref(frame);pending_frame=0;return;
        }
        const uint8_t *upload_pixels=rgba;
        if(!frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame convert format=%d stride=%d\n",frame->format,frame->linesize[0]);
        if(frame->format==AV_PIX_FMT_RGBA&&frame->linesize[0]==width*4&&!mask_decoder){
            // The frame remains alive until the synchronous texture upload ends.
            // Packed RGBA needs no second CPU copy when there is no alpha mask.
            upload_pixels=frame->data[0];
        }else if(frame->format==AV_PIX_FMT_RGBA){
            for(int y=0;y<height;y++)memcpy(rgba+(size_t)y*width*4,frame->data[0]+(ptrdiff_t)y*frame->linesize[0],(size_t)width*4);
        }else{
            scaler=sws_getCachedContext(scaler,width,height,frame->format,width,height,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
            if(!scaler){finish(runtime);return;}uint8_t *out[]={rgba};int stride[]={(width*4+31)&~31};
            sws_scale(scaler,(const uint8_t *const *)frame->data,frame->linesize,0,height,out,stride);
            if(stride[0]!=width*4)for(int y=1;y<height;y++)
                memmove(rgba+(size_t)y*width*4,rgba+(size_t)y*stride[0],(size_t)width*4);
        }
        if(!frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame converted; mask/upload begin\n");
        uint64_t mask_start=sceKernelGetProcessTimeWide();
        if(mask_at_time(due)<0){sceClibPrintf("[video] alpha mask decode failed\n");finish(runtime);return;}
        perf.mask_us+=sceKernelGetProcessTimeWide()-mask_start;
        uint64_t upload_start=sceKernelGetProcessTimeWide();perf.prepare_us+=upload_start-prepare_start;
        if(async_mode) {
            if(!video_queue_push(&frame_queue,upload_pixels,due))return;
        }
        else if(*id)art3m1s_runtime_upload_video_layer_frame(runtime,id,width,height,upload_pixels,(size_t)width*height*4);
        else{
#ifdef ART3M1S_HOST_GXM
            texture=host_gxm_video_rgba(texture,width,height,upload_pixels);
            if(!texture){finish(runtime);return;}
#else
            GLint previous;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
            if(!texture){glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,upload_pixels);}
            else{glBindTexture(GL_TEXTURE_2D,texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,upload_pixels);}
            glBindTexture(GL_TEXTURE_2D,previous);
#endif
        }
        if(!frames_uploaded)av_log(NULL,AV_LOG_INFO,"[video] first frame uploaded\n");
        perf.upload_us+=sceKernelGetProcessTimeWide()-upload_start;perf.frames++;report_video_perf(0);
        frames_uploaded++;av_frame_unref(frame);pending_frame=0;return;
    }
}
void host_video_tick(void *runtime){
    if(pending) {
        char *command=pending;pending=NULL;
        cJSON *j=cJSON_Parse(command);int r=j?open_video(j,runtime):-1;cJSON_Delete(j);free(command);
        if(r<0){sceClibPrintf("[video] open failed %d\n",r);finish(runtime);return;}
#ifdef ART3M1S_HOST_GXM
        // Start with silent, non-looping Theora layers. Hardware decoders and
        // audiovisual clocks retain their existing main-thread lifecycle.
        if(decoder->codec_id==AV_CODEC_ID_THEORA && *id && !loop &&
           av_find_best_stream(input.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)<0) {
            size_t bytes=(size_t)width*height*4;
            host_media_resource_event(0,bytes);async_pixels=av_malloc(bytes);host_media_resource_commit(&async_charge,bytes,async_pixels!=NULL);
            if(async_pixels && video_queue_init(&frame_queue,bytes)==0) {
                async_mode=1;async_clock=0;async_last_pts=0;async_presented=0;async_upload_us=0;async_skipped_conversion=0;
                AVRational rate=av_guess_frame_rate(input.format,input.format->streams[stream],NULL);
                async_frame_us=rate.num>0&&rate.den>0?av_rescale_q(1,av_inv_q(rate),(AVRational){1,1000000}):33333;
                int64_t duration=input.format->streams[stream]->duration;
                async_duration=duration>0?av_rescale_q(duration,input.format->streams[stream]->time_base,(AVRational){1,1000000}):0;
                pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,1024*1024);
                int result=pthread_create(&decode_worker,&attr,video_decode_worker,NULL);pthread_attr_destroy(&attr);
                if(result) {async_mode=0;video_queue_destroy(&frame_queue);av_freep(&async_pixels);host_media_resource_release(&async_charge);}
                else av_log(NULL,AV_LOG_INFO,"[video-async] Theora color/mask worker queue=3 bytes=%u\n",(unsigned)(4*bytes));
            } else {av_freep(&async_pixels);host_media_resource_release(&async_charge);}
        }
#endif
    }
    if(!active)return;
    if(!async_mode){decode_video_tick(runtime);return;}
    uint64_t now=sceKernelGetProcessTimeWide();
    if(!async_clock){if(!video_queue_prefilled(&frame_queue))return;async_clock=now;}
    int64_t pts=0,clock=(int64_t)(now-async_clock);
    int result=video_queue_take(&frame_queue,clock,async_pixels,&pts);
    if(result==1) {
        uint64_t upload_start=sceKernelGetProcessTimeWide();
        if(art3m1s_runtime_upload_video_layer_frame(runtime,id,width,height,async_pixels,(size_t)width*height*4)<=0){finish(runtime);return;}
        async_upload_us+=sceKernelGetProcessTimeWide()-upload_start;
        async_last_pts=pts;async_presented++;
    } else if(result==2 && (!async_presented || clock>=async_last_pts+async_frame_us)) finish(runtime);
}
#ifdef ART3M1S_HOST_GXM
void host_video_present_idle(void){
    unsigned image=direct_mode?host_video_direct_texture():texture;
    float u=1,v=1;if(direct_mode)host_video_direct_uv(&u,&v);
    if(active && !*id && image)host_gxm_video_draw(image,u,v);
}
#else
extern void __real_vglSwapBuffers(GLboolean);
void __wrap_vglSwapBuffers(GLboolean dialog){
    GLuint movie_texture=direct_mode?host_video_direct_texture():texture;
    if(active && !*id && movie_texture){
        GLint program,fbo,unit,mode;glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&fbo);glGetIntegerv(GL_ACTIVE_TEXTURE,&unit);glGetIntegerv(GL_MATRIX_MODE,&mode);
        glPushAttrib(GL_ALL_ATTRIB_BITS);glUseProgram(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glActiveTexture(GL_TEXTURE0);
        glViewport(0,0,960,544);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
        glMatrixMode(GL_PROJECTION);glPushMatrix();glLoadIdentity();glOrtho(0,960,544,0,-1,1);glMatrixMode(GL_MODELVIEW);glPushMatrix();glLoadIdentity();
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,movie_texture);glColor4f(1,1,1,1);
        float u=1,v=1;if(direct_mode)host_video_direct_uv(&u,&v);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(u,0);glVertex2f(960,0);glTexCoord2f(u,v);glVertex2f(960,544);glTexCoord2f(0,v);glVertex2f(0,544);glEnd();
        glPopMatrix();glMatrixMode(GL_PROJECTION);glPopMatrix();glMatrixMode(mode);glPopAttrib();glActiveTexture(unit);glUseProgram(program);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    }
    uint64_t present_start=sceKernelGetProcessTimeWide();
    __real_vglSwapBuffers(dialog);
    if(active){perf.present_us+=sceKernelGetProcessTimeWide()-present_start;perf.presents++;}
}
void host_video_present_idle(void){if(active && !*id && (texture||host_video_direct_texture()))__wrap_vglSwapBuffers(GL_FALSE);}
#endif
