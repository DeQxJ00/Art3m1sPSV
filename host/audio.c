#include "audio.h"
#include "audio_vorbis.h"
#include "resource_ledger.h"
#include "audio_mix.h"
#include "thread_perf.h"
#include "media_io.h"
#include "files.h"
#include "video.h"
#include "cJSON.h"
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <psp2/audioout.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <errno.h>
#define TRACKS 16
#define BLOCK 2048 // 42.7 ms: more scheduling margin without the native 106.7 ms block.
typedef struct Track {
    HostVorbis *vorbis;
    int rate,channels;
    int16_t integer_samples[8192*2];
    HostMediaInput input;
    AVCodecContext *codec;
    SwrContext *resample;
    AVPacket *packet;
    AVFrame *frame;
    int stream,active,channel,loop,draining,pending_frame,voice_hint,preparing;
    char id[128],loop_path[512];
    float samples[16384];
    int available,cursor;
    HostAudioEnvelope envelope;
    float pan;
    uint64_t generation;
} Track;
static char *audio_copy_string(const char *s){
    const size_t bytes=strlen(s)+1;char *p=host_audio_alloc(bytes,0);
    if(p)memcpy(p,s,bytes);return p;
}
static void audio_free_string(char *s){if(s)host_audio_free(s,strlen(s)+1);}
typedef struct Command { char *kind,*json; uint64_t generation; struct Command *next; } Command;
typedef struct Prepared {
    char id[128],kind[64],file[512],resolved[512],path[512];
    uint64_t generation;
    HostVorbis *vorbis;
    int rate,channels,result;
    struct Prepared *next;
} Prepared;
typedef struct Finished { char id[128]; uint64_t generation,decoded_at_us; struct Finished *next; } Finished;
typedef struct Generation { char id[128]; uint64_t value; struct Generation *next; } Generation;
static Generation *generations;
static uint64_t sequence;
// Caller holds mutex. IDs retain their latest operation even after playback ends.
static Generation *generation_for(const char *id){
    for(Generation *g=generations;g;g=g->next)if(!strcmp(g->id,id))return g;
    Generation *g=host_audio_alloc(sizeof(*g),1);if(!g)return NULL;
    snprintf(g->id,sizeof(g->id),"%s",id);g->next=generations;generations=g;return g;
}
static Track tracks[TRACKS];
// The reference SceAudiodec wrapper initializes a one-stream library per codec.
// Keep at most one accelerated track alive; other tracks retain software decode.
static Track *hardware_owner;
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static Command *first,*last;
static Finished *finished;
// Worker-owned completion stages: publish after the following output submission.
static Finished *decoded_finished,*submitted_finished;
static pthread_t worker;
static int running,started;
static pthread_t prepare_worker;
static int prepare_started;
static pthread_cond_t prepare_changed=PTHREAD_COND_INITIALIZER;
// Count includes the loading job and ready results, not just queued requests.
static Prepared *prepare_first,*prepare_last,*ready_first,*ready_last;
static unsigned prepare_count;
static float volumes[4]={1,1,1,1};
static uint64_t resample_work_us;
static int audio_resample(SwrContext *context,uint8_t **out,int out_count,const uint8_t **in,int in_count){
    uint64_t start=sceKernelGetProcessTimeWide();
    int result=swr_convert(context,out,out_count,in,in_count);
    resample_work_us+=sceKernelGetProcessTimeWide()-start;
    return result;
}
extern void art3m1s_runtime_notify_sound_finished(void *,const char *);
static const char *str(cJSON *j,const char *key){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsString(v)?v->valuestring:NULL;}
static double num(cJSON *j,const char *key,double fallback){const cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);return cJSON_IsNumber(v)?v->valuedouble:fallback;}
static float gain_value(double v){return fmaxf(0,fminf(1,v>1?v/1000:v));}
static void close_track(Track *t){
    if(t->active)av_log(NULL,AV_LOG_INFO,"[audio-lifecycle] phase=close at_us=%llu id=%s generation=%llu channel=%d voice_hint=%d\n",
        (unsigned long long)sceKernelGetProcessTimeWide(),t->id,(unsigned long long)t->generation,t->channel,t->voice_hint);
    host_vorbis_close(t->vorbis);avcodec_free_context(&t->codec);if(hardware_owner==t)hardware_owner=NULL;swr_free(&t->resample);av_packet_free(&t->packet);av_frame_free(&t->frame);host_media_input_close(&t->input);memset(t,0,sizeof(*t));}
static void complete(Track *t){
    Finished *f=host_audio_alloc(sizeof(*f),1);
    if(f){snprintf(f->id,sizeof(f->id),"%s",t->id);f->generation=t->generation;f->decoded_at_us=sceKernelGetProcessTimeWide();f->next=decoded_finished;decoded_finished=f;}
    close_track(t);
}
static int resolve(char *out,const char *path){
    if(!path||!*path)return -1;
    const char *extensions[]={"",".ogg",".oga",".wav",".mp3",".m4a"};
    for(int i=0;i<6;i++){snprintf(out,512,"%s%s",path,extensions[i]);if(host_read(out,NULL,0,-1)>=0)return 0;}
    return -1;
}
static int generation_current(const char *id,uint64_t generation){
    for(Generation *g=generations;g;g=g->next)if(!strcmp(g->id,id))return g->value==generation;
    return 0;
}
static int prepare_cancelled(void *opaque){
    Prepared *p=opaque;
    pthread_mutex_lock(&mutex);int stop=!running||!generation_current(p->id,p->generation);pthread_mutex_unlock(&mutex);
    return stop;
}
static void free_prepared(Prepared *p){
    host_vorbis_close(p->vorbis);host_audio_free(p,sizeof(*p));
    pthread_mutex_lock(&mutex);--prepare_count;pthread_mutex_unlock(&mutex);
}
static void *prepare_audio(void *unused){
    for(;;){
        pthread_mutex_lock(&mutex);
        while(running&&!prepare_first)pthread_cond_wait(&prepare_changed,&mutex);
        if(!running){pthread_mutex_unlock(&mutex);break;}
        Prepared *p=prepare_first;prepare_first=p->next;if(!prepare_first)prepare_last=NULL;
        pthread_mutex_unlock(&mutex);p->next=NULL;
        uint64_t at=sceKernelGetProcessTimeWide();
        p->result=-1;
        if(!prepare_cancelled(p)){
            p->result=resolve(p->path,p->resolved);if(p->result<0)p->result=resolve(p->path,p->file);
            if(p->result>=0&&!prepare_cancelled(p))
                p->vorbis=host_vorbis_open_preloaded(p->path,&p->rate,&p->channels,prepare_cancelled,p);
        }
        int cancelled=prepare_cancelled(p);
        av_log(NULL,AV_LOG_INFO,"[audio-preload] at_us=%llu id=%s generation=%llu cancelled=%d bytes=%u total_bytes=%u prepare_us=%llu result=%d vorbis=%d file=%s\n",
            (unsigned long long)sceKernelGetProcessTimeWide(),p->id,(unsigned long long)p->generation,cancelled,(unsigned)host_vorbis_preloaded_bytes(p->vorbis),
            (unsigned)host_vorbis_preload_bytes_in_use(),(unsigned long long)(sceKernelGetProcessTimeWide()-at),p->result,p->vorbis!=NULL,p->path);
        if(cancelled){free_prepared(p);continue;}
        pthread_mutex_lock(&mutex);
        if(ready_last)ready_last->next=p;else ready_first=p;ready_last=p;
        pthread_mutex_unlock(&mutex);
    }
    return NULL;
}
static int queue_prepare(Track *t,const char *kind,cJSON *j){
    if(!prepare_started)return 0;
    Prepared *p=host_audio_alloc(sizeof(*p),1);if(!p)return 0;
    snprintf(p->id,sizeof(p->id),"%s",t->id);p->generation=t->generation;
    snprintf(p->kind,sizeof(p->kind),"%s",kind);
    const char *file=str(j,"file"),*resolved=str(j,"resolved_file");
    snprintf(p->file,sizeof(p->file),"%s",file?file:"");
    snprintf(p->resolved,sizeof(p->resolved),"%s",resolved?resolved:"");
    pthread_mutex_lock(&mutex);
    if(prepare_count>=TRACKS){pthread_mutex_unlock(&mutex);host_audio_free(p,sizeof(*p));return 0;}
    ++prepare_count;
    if(prepare_last)prepare_last->next=p;else prepare_first=p;prepare_last=p;
    pthread_cond_signal(&prepare_changed);pthread_mutex_unlock(&mutex);
    t->active=1;t->preparing=1;return 1;
}
static int create_audio_codec(Track *t,const AVCodec *codec){
    t->codec=avcodec_alloc_context3(codec);if(!t->codec)return AVERROR(ENOMEM);
    int r=avcodec_parameters_to_context(t->codec,t->input.format->streams[t->stream]->codecpar);
    if(r<0)return r;
    t->codec->thread_count=1;
    return avcodec_open2(t->codec,codec,NULL);
}
static int prime_audio(Track *t){
    for(int i=0;i<128;i++){
        int r=avcodec_receive_frame(t->codec,t->frame);
        if(r!=AVERROR(EAGAIN))return r;
        while((r=av_read_frame(t->input.format,t->packet))>=0){
            if(t->packet->stream_index==t->stream)break;
            av_packet_unref(t->packet);
        }
        if(r<0)return r;
        r=avcodec_send_packet(t->codec,t->packet);av_packet_unref(t->packet);
        if(r<0)return r;
    }
    return AVERROR_INVALIDDATA;
}
static int open_decoder(Track *t,const char *path){
    int r;
    if(!t->vorbis)t->vorbis=host_vorbis_open(path,&t->rate,&t->channels);
    if(t->vorbis){
        if(t->rate!=48000){
            AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO, layout;
            av_channel_layout_default(&layout,t->channels);
            r=swr_alloc_set_opts2(&t->resample,&stereo,AV_SAMPLE_FMT_FLT,48000,&layout,AV_SAMPLE_FMT_S16,t->rate,0,NULL);
            av_channel_layout_uninit(&layout);
            if(r<0 || (r=swr_init(t->resample))<0)return r;
        }
        av_log(NULL,AV_LOG_INFO,"[audio] decoder=tremor-fixed rate=%d channels=%d resample=%d path=%s\n",t->rate,t->channels,t->resample!=NULL,path);
        t->active=1;return 0;
    }
    r=host_media_input_open(&t->input,path);if(r<0)return r;
    const AVCodec *codec=NULL;
    r=av_find_best_stream(t->input.format,AVMEDIA_TYPE_AUDIO,-1,-1,&codec,0);if(r<0)return r;
    t->stream=r;t->packet=av_packet_alloc();t->frame=av_frame_alloc();
    if(!t->packet||!t->frame)return AVERROR(ENOMEM);
#ifdef ART3M1S_EXPERIMENTAL_VITA_AUDIO
    const AVCodec *hardware=NULL;
    if(!hardware_owner&&!strcmp(t->id,"__video_audio")){
        enum AVCodecID id=t->input.format->streams[t->stream]->codecpar->codec_id;
        if(id==AV_CODEC_ID_AAC)hardware=avcodec_find_decoder_by_name("aac_vita");
        if(id==AV_CODEC_ID_MP3)hardware=avcodec_find_decoder_by_name("mp3_vita");
    }
    if(hardware){
        av_log(NULL,AV_LOG_INFO,"[audio] accelerator open %s path=%s\n",hardware->name,path);
        r=create_audio_codec(t,hardware);
        if(r>=0)r=prime_audio(t);
        if(r>=0){hardware_owner=t;t->pending_frame=1;codec=hardware;}
        else{
            av_log(NULL,AV_LOG_WARNING,"[audio] accelerator first frame failed %d; reopen software\n",r);
            avcodec_free_context(&t->codec);av_frame_unref(t->frame);av_packet_unref(t->packet);
            host_media_input_close(&t->input);
            if((r=host_media_input_open(&t->input,path))<0)return r;
            r=av_find_best_stream(t->input.format,AVMEDIA_TYPE_AUDIO,-1,-1,&codec,0);if(r<0)return r;t->stream=r;
        }
    }
#endif
    if(!t->codec&&(r=create_audio_codec(t,codec))<0)return r;
    av_log(NULL,AV_LOG_INFO,"[audio] decoder=%s first_frame=%d rate=%d channels=%d path=%s\n",codec->name,t->pending_frame,t->codec->sample_rate,t->codec->ch_layout.nb_channels,path);
    t->rate=t->codec->sample_rate;t->channels=t->codec->ch_layout.nb_channels;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    r=swr_alloc_set_opts2(&t->resample,&stereo,AV_SAMPLE_FMT_FLT,48000,&t->codec->ch_layout,t->codec->sample_fmt,t->codec->sample_rate,0,NULL);
    if(r<0 || (r=swr_init(t->resample))<0)return r;
    t->active=1;return 0;
}
static void fade(Track *t,float target,int ms){if(ms<0)ms=0;if(ms>3600000)ms=3600000;t->envelope.target=target;t->envelope.left=ms*48;t->envelope.step=t->envelope.left?(target-t->envelope.gain)/t->envelope.left:0;if(!t->envelope.left)t->envelope.gain=target;}
static Track *find_track(const char *id){for(int i=0;i<TRACKS;i++)if(tracks[i].active && !strcmp(tracks[i].id,id))return &tracks[i];return NULL;}
static void log_play(Track *t,const char *kind,const char *path){
    // Diagnostic only: game scripts can put speech on SE channels.
    t->voice_hint=t->channel==3||strstr(path,":vo/")!=NULL||strstr(path,"/vo/")!=NULL;
    av_log(NULL,AV_LOG_INFO,"[audio-lifecycle] phase=play at_us=%llu id=%s generation=%llu kind=%s channel=%d voice_hint=%d loop=%d rate=%d file=%s\n",
        (unsigned long long)sceKernelGetProcessTimeWide(),t->id,(unsigned long long)t->generation,kind,t->channel,t->voice_hint,t->loop,t->rate,path);
}
static void publish_prepared(void){
    pthread_mutex_lock(&mutex);Prepared *p=ready_first;ready_first=ready_last=NULL;pthread_mutex_unlock(&mutex);
    while(p){
        Prepared *next=p->next;
        pthread_mutex_lock(&mutex);int current=generation_current(p->id,p->generation);pthread_mutex_unlock(&mutex);
        Track *t=find_track(p->id);
        if(current&&t&&t->preparing&&t->generation==p->generation){
            t->preparing=0;t->vorbis=p->vorbis;p->vorbis=NULL;t->rate=p->rate;t->channels=p->channels;
            int r=p->result;
            if(r>=0)r=open_decoder(t,p->path);
            if(r<0){av_log(NULL,AV_LOG_WARNING,"[audio] prepared open failed id=%s result=%d\n",p->id,r);complete(t);}
            else log_play(t,p->kind,p->path);
        }
        free_prepared(p);p=next;
    }
}
static void apply_command(const char *kind,cJSON *j,uint64_t generation){
    if(!strcmp(kind,"audio_set_volume")){const char *s=str(j,"channel");const char *names[]={"master","bgm","se","voice"};for(int i=0;s&&i<4;i++)if(!strcmp(s,names[i]))volumes[i]=gain_value(num(j,"value",1));return;}
    if(!strcmp(kind,"audio_stop_all")){for(int i=0;i<TRACKS;i++)close_track(&tracks[i]);return;}
    int bgm=strstr(kind,"audio_bgm_")==kind;
    const char *id=bgm?"":str(j,"id");if(!id)return;
    Track *t=find_track(id);
    if(strstr(kind,"_play") || strstr(kind,"_crossfade")){
        int cross=strstr(kind,"_crossfade")!=NULL;
        if(t && cross){fade(t,0,(int)num(j,"time_ms",0));t->envelope.stop_at_zero=1;snprintf(t->id,sizeof(t->id),"__old_bgm");t=NULL;}
        if(t)close_track(t);
        if(!t)for(int i=0;i<TRACKS;i++)if(!tracks[i].active){t=&tracks[i];break;}
        if(!t){sceClibPrintf("[audio] track limit reached\n");return;}
        snprintf(t->id,sizeof(t->id),"%s",id);
        t->generation=generation;
        t->channel=bgm?1:strstr(kind,"voice")?3:2;
        t->loop=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"loop"));
        t->pan=num(j,"pan",0);if(fabsf(t->pan)>1)t->pan/=1000;
        int ms=num(j,cross?"time_ms":"fade_ms",0);
        float gain=gain_value(num(j,"gain",1));t->envelope.gain=ms?0:gain;fade(t,gain,ms);
        if(!bgm&&!t->loop&&strcmp(id,"__video_audio")&&queue_prepare(t,kind,j))return;
        char path[512];
        int r=resolve(path,str(j,"resolved_file"));if(r<0)r=resolve(path,str(j,"file"));
        if(r>=0)r=open_decoder(t,path);
        if(r<0){sceClibPrintf("[audio] open failed %s: %d\n",str(j,"file"),r);complete(t);return;}
        if(resolve(t->loop_path,str(j,"resolved_loop_file"))<0 && resolve(t->loop_path,str(j,"loop_file"))<0)t->loop_path[0]=0;
        log_play(t,kind,path);
    }else if(t && strstr(kind,"_stop")){int ms=num(j,"fade_ms",0);if(ms&&!t->preparing){fade(t,0,ms);t->envelope.stop_at_zero=1;}else close_track(t);
    }else if(t && strstr(kind,"_fade")){fade(t,gain_value(num(j,"gain",t->envelope.gain)),num(j,"time_ms",0));
    }else if(t && strstr(kind,"_pan")){t->pan=num(j,"pan",0);if(fabsf(t->pan)>1)t->pan/=1000;}
}
static int decode_vorbis(Track *t){
    int rewound=0;
    for(;;){
        int limit=t->resample?(int)((int64_t)4096*t->rate/48000):8192;
        if(limit<1)limit=1;if(limit>8192)limit=8192;
        int r=host_vorbis_read(t->vorbis,t->integer_samples,limit);
        if(r<0)return r;
        if(r>0){
            if(t->resample){
                uint8_t *out=(uint8_t *)t->samples;
                const uint8_t *in=(const uint8_t *)t->integer_samples;
                r=audio_resample(t->resample,&out,8192,&in,r);
                if(r<0)return r;
                if(!r)continue;
            }else if(t->channels==2){
                for(int i=0;i<r*2;i++)t->samples[i]=t->integer_samples[i]*(1.0f/32768.0f);
            }else{
                // Match swresample's default mono-to-stereo matrix (-3 dB per side).
                for(int i=0;i<r;i++)t->samples[2*i]=t->samples[2*i+1]=t->integer_samples[i]*(0.7071067811865475f/32768.0f);
            }
            t->available=r;t->cursor=0;return r;
        }
        if(t->resample){
            uint8_t *out=(uint8_t *)t->samples;
            r=audio_resample(t->resample,&out,8192,NULL,0);
            if(r<0)return r;
            if(r>0){t->available=r;t->cursor=0;return r;}
        }
        if(!t->loop || t->loop_path[0])return 0;
        if(rewound++ || host_vorbis_rewind(t->vorbis)<0)return -1;
        if(t->resample){swr_close(t->resample);if(swr_init(t->resample)<0)return -1;}
    }
}
static int decode(Track *t){
    if(t->vorbis){
        int r=decode_vorbis(t);
        if(r || !t->loop || !t->loop_path[0])return r;
        char path[512];strcpy(path,t->loop_path);t->loop_path[0]=0;
        host_vorbis_close(t->vorbis);t->vorbis=NULL;swr_free(&t->resample);
        if(open_decoder(t,path)<0)return -1;
        return decode(t);
    }
    static int debug_calls=0;
    int debug=debug_calls++<4;
    if(debug)sceClibPrintf("[audio] decode begin\n");
    for(;;){
        int r=t->pending_frame?0:avcodec_receive_frame(t->codec,t->frame);t->pending_frame=0;
        if(debug)sceClibPrintf("[audio] receive %d\n",r);
        if(r>=0){uint8_t *out=(uint8_t *)t->samples;r=audio_resample(t->resample,&out,8192,(const uint8_t **)t->frame->extended_data,t->frame->nb_samples);av_frame_unref(t->frame);if(r<=0){if(r<0)return r;continue;}t->available=r;t->cursor=0;return r;}
        if(r==AVERROR_EOF){
            uint8_t *out=(uint8_t *)t->samples;r=audio_resample(t->resample,&out,8192,NULL,0);
            if(r>0){t->available=r;t->cursor=0;return r;}
            if(!t->loop)return 0;
            if(t->loop_path[0]){
                char path[512];strcpy(path,t->loop_path);t->loop_path[0]=0;
                avcodec_free_context(&t->codec);if(hardware_owner==t)hardware_owner=NULL;swr_free(&t->resample);av_packet_free(&t->packet);av_frame_free(&t->frame);host_media_input_close(&t->input);
                if(open_decoder(t,path)<0)return -1;
                if(t->vorbis)return decode(t);
            }else{if(av_seek_frame(t->input.format,t->stream,0,AVSEEK_FLAG_BACKWARD)<0)return -1;avcodec_flush_buffers(t->codec);swr_close(t->resample);if(swr_init(t->resample)<0)return -1;}
            t->draining=0;continue;
        }
        if(r!=AVERROR(EAGAIN))return r;
        if(t->draining)return -1;
        while((r=av_read_frame(t->input.format,t->packet))>=0){if(t->packet->stream_index==t->stream)break;av_packet_unref(t->packet);}
        if(r<0){if(r!=AVERROR_EOF)return r;t->draining=1;r=avcodec_send_packet(t->codec,NULL);}
        else {if(debug)sceClibPrintf("[audio] send %d bytes\n",t->packet->size);r=avcodec_send_packet(t->codec,t->packet);av_packet_unref(t->packet);}
        if(r<0)return r;
    }
}
static void *audio_worker(void *unused){
    HostThreadPerf thread_perf={0};host_thread_perf("audio",&thread_perf,0);resample_work_us=0;
    int port=sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,BLOCK,48000,SCE_AUDIO_OUT_MODE_STEREO);
    if(port<0){sceClibPrintf("[audio] output port failed: %x\n",port);return NULL;}
    av_log(NULL,AV_LOG_INFO,"[audio] block_frames=%d buffers=2 mix=span-neon vorbis=fixed-point\n",BLOCK);
    int volume[2]={SCE_AUDIO_VOLUME_0DB,SCE_AUDIO_VOLUME_0DB};sceAudioOutSetVolume(port,SCE_AUDIO_VOLUME_FLAG_L_CH|SCE_AUDIO_VOLUME_FLAG_R_CH,volume);
    float mix[BLOCK*2];int16_t output[2][BLOCK*2];unsigned blocks=0;int buffer=0;
    uint64_t report_at=sceKernelGetProcessTimeWide(),work_us=0,max_us=0;
    unsigned work_blocks=0,late_blocks=0,clipped_samples=0,output_errors=0;
    uint64_t command_us=0,decode_us=0;unsigned decode_calls=0;
    float window_peak=0;
    for(;;){
        uint64_t block_start=sceKernelGetProcessTimeWide();
        pthread_mutex_lock(&mutex);
        if(!running){pthread_mutex_unlock(&mutex);break;}
        Command *cmd=first;first=last=NULL;pthread_mutex_unlock(&mutex);
        while(cmd){Command *next=cmd->next;cJSON *j=cJSON_Parse(cmd->json);if(j)apply_command(cmd->kind,j,cmd->generation);cJSON_Delete(j);audio_free_string(cmd->kind);audio_free_string(cmd->json);host_audio_free(cmd,sizeof(*cmd));cmd=next;}
        publish_prepared();
        command_us+=sceKernelGetProcessTimeWide()-block_start;
        memset(mix,0,sizeof(mix));
        for(int n=0;n<TRACKS;n++){Track *t=&tracks[n];if(!t->active||t->preparing)continue;
            float volume=volumes[0]*volumes[t->channel];
            float left=volume*(t->pan>0?1-t->pan:1),right=volume*(t->pan<0?1+t->pan:1);
            for(int i=0;i<BLOCK;){
                if(t->cursor>=t->available){uint64_t decode_start=sceKernelGetProcessTimeWide();int decoded=decode(t);decode_us+=sceKernelGetProcessTimeWide()-decode_start;decode_calls++;if(decoded<=0){sceClibPrintf("[audio] finished id=%s decode=%d\n",t->id,decoded);if(strcmp(t->id,"__old_bgm"))complete(t);else close_track(t);break;}}
                int count=t->available-t->cursor;if(count>BLOCK-i)count=BLOCK-i;
                int consumed=host_audio_mix(mix+2*i,t->samples+2*t->cursor,count,&t->envelope,left,right);
                t->cursor+=consumed;i+=consumed;
                if(t->envelope.stop_at_zero && t->envelope.gain<=0){close_track(t);break;}
            }
        }
        float peak=host_audio_pack(output[buffer],mix,BLOCK*2,&clipped_samples);
        window_peak=fmaxf(window_peak,peak);
        uint64_t elapsed=sceKernelGetProcessTimeWide()-block_start;
        work_us+=elapsed;work_blocks++;if(elapsed>max_us)max_us=elapsed;
        if(elapsed>BLOCK*1000000ULL/48000)late_blocks++;
        int r=sceAudioOutOutput(port,output[buffer]);buffer^=1;
        if(r<0)output_errors++;
        pthread_mutex_lock(&mutex);
        while(submitted_finished){Finished *f=submitted_finished;submitted_finished=f->next;f->next=finished;finished=f;}
        pthread_mutex_unlock(&mutex);
        submitted_finished=decoded_finished;decoded_finished=NULL;
        uint64_t now=sceKernelGetProcessTimeWide();
        if(now-report_at>=5000000){
            int active_tracks=0,active_voice=0;for(int n=0;n<TRACKS;n++){active_tracks+=tracks[n].active!=0;active_voice+=tracks[n].active&&tracks[n].voice_hint;}
            av_log(NULL,AV_LOG_INFO,"[audio-detail] at_us=%llu resample_us=%llu active_tracks=%d active_voice_hint=%d work_wall_permille=%llu (wall time, not CPU utilization)\n",(unsigned long long)now,(unsigned long long)resample_work_us,active_tracks,active_voice,(unsigned long long)(work_us*1000/(now-report_at)));
            resample_work_us=0;host_thread_perf("audio",&thread_perf,1);
            av_log(NULL,AV_LOG_INFO,"[audio-perf] blocks=%u average_work_us=%llu max_work_us=%llu over_budget=%u output=%d peak=%.4f clipped_samples=%u output_errors=%u command_us=%llu decode_us=%llu mix_pack_us=%llu decode_calls=%u\n",work_blocks,(unsigned long long)(work_us/work_blocks),(unsigned long long)max_us,late_blocks,r,window_peak,clipped_samples,output_errors,(unsigned long long)command_us,(unsigned long long)decode_us,(unsigned long long)(work_us-command_us-decode_us),decode_calls);
            report_at=now;work_us=max_us=0;work_blocks=late_blocks=0;
            window_peak=0;clipped_samples=output_errors=0;command_us=decode_us=0;decode_calls=0;
        }
        if(peak>0.0001f && blocks++%240==0)sceClibPrintf("[audio] PCM output peak %.3f result %d\n",peak,r);
    }
    for(int i=0;i<TRACKS;i++)close_track(&tracks[i]);sceAudioOutOutput(port,NULL);sceAudioOutReleasePort(port);return NULL;
}
int host_audio_start(void){
    pthread_mutex_lock(&mutex);running=1;pthread_mutex_unlock(&mutex);
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,512*1024);
    prepare_started=pthread_create(&prepare_worker,&attr,prepare_audio,NULL)==0;
    if(!prepare_started)av_log(NULL,AV_LOG_WARNING,"[audio] preparation thread unavailable; streaming fallback\n");
    int r=pthread_create(&worker,&attr,audio_worker,NULL);pthread_attr_destroy(&attr);started=r==0;
    if(r){pthread_mutex_lock(&mutex);running=0;pthread_cond_broadcast(&prepare_changed);pthread_mutex_unlock(&mutex);if(prepare_started)pthread_join(prepare_worker,NULL);prepare_started=0;}
    return r;
}
void host_media_command(const char *kind,const char *json){
    sceClibPrintf("[media-command] %s %s\n",kind,json);
    if(strncmp(kind,"audio_",6)){host_video_command(kind,json);return;}
    Command *cmd=host_audio_alloc(sizeof(*cmd),1);if(!cmd)return;cmd->kind=audio_copy_string(kind);cmd->json=audio_copy_string(json);
    if(!cmd->kind||!cmd->json){audio_free_string(cmd->kind);audio_free_string(cmd->json);host_audio_free(cmd,sizeof(*cmd));return;}
    cJSON *j=cJSON_Parse(json);
    const char *id=strstr(kind,"audio_bgm_")==kind?"":str(j,"id");
    pthread_mutex_lock(&mutex);
    if(!strcmp(kind,"audio_stop_all")){for(Generation *g=generations;g;g=g->next)g->value=++sequence;}
    else if(id && (strstr(kind,"_play")||strstr(kind,"_crossfade")||strstr(kind,"_stop"))){Generation *g=generation_for(id);if(g)cmd->generation=g->value=++sequence;}
    if(last)last->next=cmd;else first=cmd;last=cmd;pthread_mutex_unlock(&mutex);cJSON_Delete(j);
}
void host_audio_poll(void *runtime){
    pthread_mutex_lock(&mutex);Finished *f=finished;finished=NULL;pthread_mutex_unlock(&mutex);
    while(f){Finished *next=f->next;pthread_mutex_lock(&mutex);Generation *g=generation_for(f->id);int current=g&&g->value==f->generation;pthread_mutex_unlock(&mutex);
        int forwarded=current&&strcmp(f->id,"__video_audio");uint64_t started=sceKernelGetProcessTimeWide();
        if(forwarded)art3m1s_runtime_notify_sound_finished(runtime,*f->id?f->id:NULL);
        av_log(NULL,AV_LOG_INFO,"[audio-lifecycle] phase=notify at_us=%llu id=%s generation=%llu current=%d forwarded=%d delay_us=%llu callback_us=%llu\n",
            (unsigned long long)started,f->id,(unsigned long long)f->generation,current,forwarded,
            (unsigned long long)(started-f->decoded_at_us),(unsigned long long)(sceKernelGetProcessTimeWide()-started));
        host_audio_free(f,sizeof(*f));f=next;}
}
void host_audio_stop(void){pthread_mutex_lock(&mutex);running=0;pthread_cond_broadcast(&prepare_changed);pthread_mutex_unlock(&mutex);
    if(started)pthread_join(worker,NULL);started=0;
    if(prepare_started)pthread_join(prepare_worker,NULL);prepare_started=0;
    while(prepare_first){Prepared *p=prepare_first;prepare_first=p->next;free_prepared(p);}prepare_last=NULL;
    while(ready_first){Prepared *p=ready_first;ready_first=p->next;free_prepared(p);}ready_last=NULL;
    while(first){Command *c=first;first=c->next;audio_free_string(c->kind);audio_free_string(c->json);host_audio_free(c,sizeof(*c));}last=NULL;
    while(finished){Finished *f=finished;finished=f->next;host_audio_free(f,sizeof(*f));}
    while(decoded_finished){Finished *f=decoded_finished;decoded_finished=f->next;host_audio_free(f,sizeof(*f));}
    while(submitted_finished){Finished *f=submitted_finished;submitted_finished=f->next;host_audio_free(f,sizeof(*f));}
    while(generations){Generation *g=generations;generations=g->next;host_audio_free(g,sizeof(*g));}
}
