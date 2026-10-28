#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
static int oracle_return_mode;
static int test_sws_scale(struct SwsContext *,const uint8_t *const [],const int [],int,int,uint8_t *const [],const int []);
#define sws_scale test_sws_scale
static int slow_decode;
static int test_receive_frame(AVCodecContext *,AVFrame *);
#define avcodec_receive_frame test_receive_frame
static int inject_empty_packets, color_packets, mask_packets;
static int test_read_frame(AVFormatContext *,AVPacket *);
#define av_read_frame test_read_frame
#include "../../host/video.c"
#undef av_read_frame
#undef avcodec_receive_frame
#undef sws_scale
static int test_read_frame(AVFormatContext *ctx,AVPacket *pkt){
    int *count=ctx==current_video->input.format?&color_packets:&mask_packets;
    if(inject_empty_packets&&(*count)++==1){
        av_packet_unref(pkt);assert(av_new_packet(pkt,0)==0);
        pkt->stream_index=ctx==current_video->input.format?current_video->stream:current_video->mask_stream;
        return 0;
    }
    return av_read_frame(ctx,pkt);
}
static int test_sws_scale(struct SwsContext *s,const uint8_t *const src[],const int stride[],int y,int h,uint8_t *const dst[],const int ds[]){
    if(oracle_return_mode&&h==16&&stride[0]==32&&ds[0]==128){
        if(oracle_return_mode==1)sws_scale(s,src,stride,y,h,dst,ds);
        return 0; // ARM wrapper success, or deliberately missing all output
    }
    return sws_scale(s,src,stride,y,h,dst,ds);
}
static int test_receive_frame(AVCodecContext *c,AVFrame *f){
    int r=avcodec_receive_frame(c,f);
    if(!r&&c==current_video->decoder&&slow_decode){struct timespec pause={0,80000000};nanosleep(&pause,NULL);}
    return r;
}
struct HostReadStream {FILE *file;};
static pthread_t main_thread;
static int live,notifications,uploads,stream_reads,fail_next_read;
static unsigned finished_skips;
static int ready_notifications;
void art3m1s_runtime_video_layer_ready(void *runtime,const char *id){
    assert(runtime==(void*)1 && id && *id);
    assert(pthread_equal(main_thread,pthread_self()));
    assert(current_video->async_mode ? current_video->async_presented==0 : current_video->frames_uploaded==0);
    ++ready_notifications;
}
static int multi_test,body_uploads,accessory_uploads;
static int fullscreen_test,priority_begins;
void host_video_prioritize_gpu(void* runtime){assert(runtime==(void*)1);assert(fullscreen_test);++priority_begins;}
static int test_yuva,yuva_uploads,test_full_range;
static int test_mapped,mapped_opens,mapped_closes;
static struct {void* owner;uint8_t* slots[VIDEO_QUEUE_SLOTS];} mapped_pools[8];
static int pool_index(void* owner,int create){
    for(int i=0;i<8;i++)if(mapped_pools[i].owner==owner)return i;
    if(create)for(int i=0;i<8;i++)if(!mapped_pools[i].owner){mapped_pools[i].owner=owner;return i;}
    return -1;
}
static unsigned expected_width=64,expected_height=64;
static int64_t last_presented;
#ifdef TEST_OGV_GROUP_CACHE
static uint8_t *group_color,*group_mask;
static size_t group_color_size,group_mask_size;
static int group_pins,group_opens,group_late;
static uint8_t *read_group_file(const char *p,size_t *size){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);uint8_t *b=malloc(*size);assert(b&&fread(b,1,*size,f)==*size);fclose(f);return b;}
void *art3m1s_ogv_cache_acquire(const char *p,const uint8_t **color,size_t *cs,const uint8_t **mask,size_t *ms){
    assert(!strcmp(p,"arrow.ogv"));++group_opens;
    if(group_late&&group_opens==1)return NULL;
    *color=group_color;*cs=group_color_size;*mask=group_mask;*ms=group_mask_size;++group_pins;return group_color;
}
void art3m1s_ogv_cache_release(void *lease){assert(lease==group_color&&group_pins>0);--group_pins;}
#endif
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
HostReadStream *host_stream_open(const char *path,int64_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t offset){stream_reads++;if(fail_next_read){fail_next_read=0;return -1;}if(fseek(s->file,offset,SEEK_SET))return -1;return fread(out,1,cap,s->file);}
void host_stream_close(HostReadStream *s){if(s){fclose(s->file);free(s);live--;}}
int host_read(const char *path,uint8_t *out,int cap,int64_t offset){struct stat st;(void)out;(void)cap;(void)offset;return stat(path,&st)?-1:(int)st.st_size;}
void host_media_command(const char *kind,const char *json){(void)kind;(void)json;assert(pthread_equal(main_thread,pthread_self()));}
void art3m1s_runtime_notify_video_finished(void *runtime,const char *name){assert(runtime==(void*)1);assert(fullscreen_test?name==NULL:multi_test?(!strcmp(name,"body")||!strcmp(name,"accessory")):!strcmp(name,"arrow"));assert(pthread_equal(main_thread,pthread_self()));notifications++;finished_skips+=current_video->async_skipped_conversion;}
int art3m1s_runtime_upload_video_layer_frame(void *runtime,const char *name,unsigned w,unsigned h,const unsigned char *p,size_t size){
    assert(pthread_equal(main_thread,pthread_self()));assert(runtime==(void*)1);
    if(multi_test){if(!strcmp(name,"body"))body_uploads++;else{assert(!strcmp(name,"accessory"));accessory_uploads++;}}
    else assert(!strcmp(name,"arrow"));
    if(multi_test&&test_yuva&&!strcmp(name,"accessory"))assert(w==32&&h==32);
    else assert(w==expected_width&&h==expected_height);
    assert(size==w*h*4);assert(p[3]>=100&&p[3]<=150);
    uploads++;last_presented=sceKernelGetProcessTimeWide();return 1;
}
int host_gxm_video_yuva_available(void){assert(pthread_equal(main_thread,pthread_self()));return test_yuva;}
int host_gxm_video_yuva_queue_open(void* session,unsigned w,unsigned h,uint8_t** slots,unsigned count){
    assert(pthread_equal(main_thread,pthread_self())&&count==VIDEO_QUEUE_SLOTS);
    if(test_mapped!=1)return 0; // also exercises allocation refusal fallback
    int k=pool_index(session,1);assert(k>=0);
    for(unsigned i=0;i<count;i++){assert(!mapped_pools[k].slots[i]);slots[i]=mapped_pools[k].slots[i]=malloc((size_t)w*h*4);assert(slots[i]);}
    mapped_opens++;return 1;
}
void host_gxm_video_yuva_queue_close(void* session){
    assert(pthread_equal(main_thread,pthread_self()));
    int k=pool_index(session,0);if(k<0)return;
    if(mapped_pools[k].slots[0])mapped_closes++;
    for(unsigned i=0;i<VIDEO_QUEUE_SLOTS;i++){
        assert(((VideoPlayer*)session)->frame_queue.pixels[i]!=mapped_pools[k].slots[i]||!mapped_pools[k].slots[i]);
        free(mapped_pools[k].slots[i]);mapped_pools[k].slots[i]=NULL;
    }
    mapped_pools[k].owner=NULL;
}
void host_gxm_video_yuva_close(void* session){assert(pthread_equal(main_thread,pthread_self()));host_gxm_video_yuva_queue_close(session);}
int host_gxm_video_yuva_upload(void* session,void *runtime,const char *name,unsigned w,unsigned h,const uint8_t *p,unsigned ranges){
    assert(test_yuva&&pthread_equal(main_thread,pthread_self()));
    assert(ranges==(test_full_range?3u:0u));
    if(test_mapped==1){
        int k=pool_index(session,0);assert(k>=0);
        int found=0;for(unsigned i=0;i<VIDEO_QUEUE_SLOTS;i++)found|=p==mapped_pools[k].slots[i];
        assert(found&&current_video->frame_queue.borrowed&&!current_video->frame_queue.owns_pixels);
    }
    size_t n=(size_t)w*h;uint8_t *converted=malloc(n*4),*a=malloc(n);assert(converted&&a);
    for(unsigned y=0;y<h;y++){
        host_video_gray_range_row(p+3*n+y*w,a+y*w,w,ranges&2);
        ((ranges&1)?host_video_yuv444_full_row:host_video_yuv444_row)(p+y*w,p+n+y*w,p+2*n+y*w,a+y*w,converted+4*y*w,w);
    }
    int r=art3m1s_runtime_upload_video_layer_frame(runtime,name,w,h,converted,n*4);
    free(converted);free(a);yuva_uploads++;return r;
}
void host_video_direct_configure(AVCodecContext *d){(void)d;assert(0);}
int host_video_direct_present(const AVFrame *f){(void)f;assert(0);return -1;}
GLuint host_video_direct_texture(void){return 0;}
void host_video_direct_uv(float *u,float *v){*u=*v=1;}
void host_video_direct_release_display(void){assert(pthread_equal(main_thread,pthread_self()));}
void host_video_direct_close_pool(void){assert(pthread_equal(main_thread,pthread_self()));}
unsigned host_gxm_video_rgba(unsigned x,int w,int h,const uint8_t*p){(void)x;assert(fullscreen_test&&w>0&&h>0&&p);++uploads;return 1;}
void host_gxm_video_delete(unsigned x){(void)x;assert(pthread_equal(main_thread,pthread_self()));}
void host_gxm_video_draw(unsigned x,float u,float v){(void)x;(void)u;(void)v;}
int main(int argc,char **argv){
    test_full_range=getenv("TEST_FULL_RANGE")!=NULL;
    if(argc==2&&!strcmp(argv[1],"oracle-arm")){
        oracle_return_mode=1;assert(video_subsample_conversion_ready(AV_PIX_FMT_YUV420P));
        oracle_return_mode=2;assert(!video_subsample_conversion_ready(AV_PIX_FMT_YUV422P));
        puts("ARM zero-return oracle: written pixels accepted, missing output rejected");return 0;
    }
    if(argc>=3){expected_width=atoi(argv[1]);expected_height=atoi(argv[2]);}
    inject_empty_packets=argc>=4&&!strcmp(argv[3],"empty");
    int stall=argc>=4&&!inject_empty_packets;
    main_thread=pthread_self();
    if(argc==2&&(!strcmp(argv[1],"multi")||!strcmp(argv[1],"multi-yuva"))){
        multi_test=1;
        test_yuva=test_mapped=!strcmp(argv[1],"multi-yuva");
        for(int round=0;round<4;round++){
            host_video_command("video_play","{\"id\":\"body\",\"file\":\"arrow.ogv\",\"skippable\":true}");
            host_video_command("video_play",test_yuva?"{\"id\":\"accessory\",\"file\":\"small.ogv\",\"skippable\":true}":"{\"id\":\"accessory\",\"file\":\"arrow.ogv\",\"skippable\":true}");
            uint64_t begin=sceKernelGetProcessTimeWide();
            do {
                host_video_tick((void*)1);host_video_skip((void*)1);
                struct timespec pause={0,1000000};nanosleep(&pause,NULL);
                assert(sceKernelGetProcessTimeWide()-begin<5000000);
            } while(layer_players);
            assert(notifications==(round+1)*2);
            assert(ready_notifications==(round+1)*2);
            assert(body_uploads>=(round+1)*10&&accessory_uploads>=(round+1)*(test_yuva?5:10));
            assert(!live&&!clock_users);
            if(test_yuva)assert(yuva_uploads==uploads&&mapped_opens==(round+1)*2&&mapped_closes==mapped_opens);
        }
        puts("Concurrent body/accessory movies complete independently; clicks do not skip layers");
        return 0;
    }
    if(argc==2&&!strcmp(argv[1],"fullscreen")){
        fullscreen_test=1;
        for(int skip=0;skip<2;++skip){
            assert(!host_video_fullscreen_active());
            host_video_command("video_play","{\"file\":\"arrow.ogv\",\"skippable\":true}");
            uint64_t start=sceKernelGetProcessTimeWide();int seen=0;
            do{
                host_video_tick((void*)1);
                if(host_video_fullscreen_active()){
                    seen=1;strcpy((layer_players?layer_players:&fullscreen_player)->id,"arrow");assert(!host_video_fullscreen_active());(layer_players?layer_players:&fullscreen_player)->id[0]=0;
                    if(skip)host_video_skip((void*)1);
                }
                struct timespec pause={0,1000000};nanosleep(&pause,NULL);
                assert(sceKernelGetProcessTimeWide()-start<5000000);
            }while((layer_players?layer_players:&fullscreen_player)->active||(layer_players?layer_players:&fullscreen_player)->pending);
            assert(seen&&!host_video_fullscreen_active()&&!(layer_players?layer_players:&fullscreen_player)->fullscreen_priority&&!live);
        }
        assert(priority_begins==2&&notifications==2&&uploads>2);
        puts("Fullscreen priority: ready-frame gate, layer exclusion, EOF and skip restoration passed");return 0;
    }
#ifdef TEST_OGV_GROUP_CACHE
    group_late=argc>=4&&!strcmp(argv[3],"cache-late");
    group_color=read_group_file("arrow.ogv",&group_color_size);group_mask=read_group_file("arrow_m.ogv",&group_mask_size);
    // Reopen repeatedly using the same group. Playback borrows both arrays;
    // close never frees cache-owned memory, and loops keep monotonic timing.
    for(int pass=0;pass<3;++pass){
        int reads_before=stream_reads;
        host_video_command("video_layer_play","{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"loop\":true}");
        uint64_t start=sceKernelGetProcessTimeWide();
        do{host_video_tick((void*)1);struct timespec pause={0,1000000};nanosleep(&pause,NULL);assert(sceKernelGetProcessTimeWide()-start<5000000);}while((layer_players?layer_players:&fullscreen_player)->async_last_pts<1200000);
        assert((layer_players?layer_players:&fullscreen_player)->input.cache_borrowed&&(layer_players?layer_players:&fullscreen_player)->mask_input.cache_borrowed&&group_pins==1);
        if(!group_late||pass>0)assert(stream_reads==reads_before&&(layer_players?layer_players:&fullscreen_player)->input.read_calls==0&&(layer_players?layer_players:&fullscreen_player)->mask_input.read_calls==0);
        assert(!(layer_players?layer_players:&fullscreen_player)->input.reader&&!(layer_players?layer_players:&fullscreen_player)->mask_input.reader);
        // Tiny streams can still reside in FFmpeg's AVIO buffer in the
        // handoff loop. A new open must read from both borrowed sources.
        if(!group_late||pass>0)assert((layer_players?layer_players:&fullscreen_player)->input.cache_reads>0&&(layer_players?layer_players:&fullscreen_player)->mask_input.cache_reads>0);
        host_video_close();assert(!group_pins&&!live&&!(layer_players?layer_players:&fullscreen_player)->async_mode);assert(!memcmp(group_color,"OggS",4)&&!memcmp(group_mask,"OggS",4));
    }
    free(group_color);free(group_mask);
    puts("Paired OGV cache: demux from borrowed bytes, mask, loop handoff, repeated playback, lease release passed");return 0;
#endif
    test_yuva=argc>=4&&!strncmp(argv[3],"yuva",4);
    test_mapped=argc>=4&&!strcmp(argv[3],"yuva-mapped")?1:argc>=4&&!strcmp(argv[3],"yuva-fallback")?2:0;
    if(argc>=4&&!strcmp(argv[3],"preload")){
        HostMediaInput probe;assert(!host_media_input_open(&probe,"arrow.ogv"));
        int before=stream_reads;int64_t position=probe.position;
        assert(!host_media_input_preload(&probe,probe.size-1));
        assert(stream_reads==before&&probe.reader&&!probe.cached);
        fail_next_read=1;assert(!host_media_input_preload(&probe,probe.size));
        assert(probe.reader&&!probe.cached&&!probe.cache_charge&&probe.position==position);
        assert(host_media_input_preload(&probe,probe.size)==(size_t)probe.size);
        assert(!probe.reader&&probe.cached&&probe.position==position);
        HostMediaInput reference;assert(!host_media_input_open(&reference,"arrow.ogv"));
        uint64_t disk_reads=probe.read_calls;AVPacket *pkt=av_packet_alloc(),*ref=av_packet_alloc();assert(pkt&&ref);
        for(int loop=0;loop<3;loop++){
            assert(av_seek_frame(probe.format,0,0,AVSEEK_FLAG_BACKWARD)>=0);
            assert(av_seek_frame(reference.format,0,0,AVSEEK_FLAG_BACKWARD)>=0);
            int count=0;
            for(;;){
                int r=av_read_frame(probe.format,pkt),rr=av_read_frame(reference.format,ref);
                assert(r==rr);
                if(r<0){assert(r==AVERROR_EOF&&count>0);break;}
                assert(pkt->size==ref->size&&pkt->pts==ref->pts&&pkt->dts==ref->dts&&pkt->flags==ref->flags);
                assert(!pkt->size||!memcmp(pkt->data,ref->data,pkt->size));count++;
                av_packet_unref(pkt);av_packet_unref(ref);
            }
        }
        assert(probe.read_calls==disk_reads&&probe.cache_reads>0);
        av_packet_free(&pkt);av_packet_free(&ref);host_media_input_close(&reference);host_media_input_close(&probe);assert(!live&&!probe.cached&&!probe.cache_charge);
        puts("Compressed preload: budget, read failure fallback, cached seek/EOF and release passed");return 0;
    }
    if(argc>=4&&!strcmp(argv[3],"mask-skip")) {
        (layer_players?layer_players:&fullscreen_player)->width=expected_width;(layer_players?layer_players:&fullscreen_player)->height=expected_height;
        size_t bytes=(size_t)(layer_players?layer_players:&fullscreen_player)->width*(layer_players?layer_players:&fullscreen_player)->height;
        uint8_t *reference=malloc(bytes*15);assert(reference);
        assert(open_mask("arrow.ogv")==0&&(layer_players?layer_players:&fullscreen_player)->mask_decoder);
        for(int i=0;i<15;i++){
            assert(mask_at_time((int64_t)i*1000000/30,1)==0);
            memcpy(reference+bytes*i,(layer_players?layer_players:&fullscreen_player)->mask_pixels,bytes);
        }
        close_mask();assert(!live);
        assert(memcmp(reference,reference+14*bytes,bytes)!=0);
        assert(open_mask("arrow.ogv")==0&&(layer_players?layer_players:&fullscreen_player)->mask_decoder);
        for(int i=0;i<15;i++){
            assert(mask_at_time((int64_t)i*1000000/30,0)==0);
            if(i%3==2){
                assert(mask_at_time((int64_t)i*1000000/30,1)==0);
                assert(!memcmp(reference+bytes*i,(layer_players?layer_players:&fullscreen_player)->mask_pixels,bytes));
            }
        }
        // EOF keeps the last converted alpha; no stale or uninitialized frame.
        assert(mask_at_time(1000000,1)==0);
        assert(!memcmp(reference+14*bytes,(layer_players?layer_players:&fullscreen_player)->mask_pixels,bytes));
        close_mask();free(reference);assert(!live);
        puts("Deferred mask conversion: advancing skipped pairs preserves selected alpha and EOF");
        return 0;
    }
    if(argc>=4&&(!strcmp(argv[3],"loop")||!strcmp(argv[3],"slow-codec")||test_yuva)) {
        slow_decode=!strcmp(argv[3],"slow-codec");
        if(test_mapped==1)for(int i=0;i<8;i++){
            host_video_command("video_layer_play","{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"loop\":true}");
            host_video_tick((void*)1);assert((layer_players?layer_players:&fullscreen_player)->async_mode);
            struct timespec fill={0,100000000};nanosleep(&fill,NULL);
            host_video_close();assert(!live&&!(layer_players?layer_players:&fullscreen_player)->async_mode&&mapped_opens==mapped_closes);
        }
        host_video_command("video_layer_play","{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"loop\":true}");
        uint64_t start=sceKernelGetProcessTimeWide();int64_t previous=0;int initial_reads=-1;
        while((layer_players?layer_players:&fullscreen_player)->async_last_pts<1200000) {
            host_video_tick((void*)1);assert((layer_players?layer_players:&fullscreen_player)->async_mode&&!notifications);
            if(initial_reads<0)initial_reads=stream_reads;
            assert(stream_reads==initial_reads&&(layer_players?layer_players:&fullscreen_player)->input.cached&&(layer_players?layer_players:&fullscreen_player)->mask_input.cached);
            assert((layer_players?layer_players:&fullscreen_player)->async_last_pts>=previous);previous=(layer_players?layer_players:&fullscreen_player)->async_last_pts;
            assert(sceKernelGetProcessTimeWide()-start<4000000);
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        }
        if(slow_decode)fprintf(stderr,"Slow decoder: uploads=%d skipped=%u\n",uploads,(layer_players?layer_players:&fullscreen_player)->async_skipped_conversion);
        assert(uploads>=30);host_video_close();assert(!live&&!(layer_players?layer_players:&fullscreen_player)->async_mode);
        if(test_yuva)assert(yuva_uploads==uploads&&yuva_uploads>=30);
        if(test_mapped==1)assert(mapped_opens==9&&mapped_closes==9);
        if(test_mapped==2)assert(!mapped_opens&&!mapped_closes);
        puts("Looping Theora+mask: monotonic PTS across >2 loops, no EOF completion, cancellation passed");
        return 0;
    }
    const char *command="{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"skippable\":true}";
    host_video_command("video_layer_play",command);
    uint64_t start=sceKernelGetProcessTimeWide();int ticks=0;
    while(!notifications){host_video_tick((void*)1);ticks++;assert(sceKernelGetProcessTimeWide()-start<5000000);struct timespec pause={0,1000000};
        if(stall&&uploads==1){pause.tv_nsec=300000000;stall=0;}
        nanosleep(&pause,NULL);}
    assert(uploads>0&&uploads<=15);if(argc<4||inject_empty_packets)assert(uploads>=10);
    else assert(finished_skips>0);
    assert(ticks>50);assert(!live);assert(!(layer_players?layer_players:&fullscreen_player)->async_mode);
    assert(sceKernelGetProcessTimeWide()-(uint64_t)last_presented>=15000);
    // Exercise shutdown while a producer is blocked on a full frame queue.
    for(int i=0;i<20;i++) {
        host_video_command("video_layer_play",command);host_video_tick((void*)1);
        assert((layer_players?layer_players:&fullscreen_player)->async_mode);struct timespec pause={0,20000000};nanosleep(&pause,NULL);
        host_video_close();assert(!live&&!(layer_players?layer_players:&fullscreen_player)->async_mode);
    }
    assert(notifications==1);
    puts("Theora+mask: main-thread-only upload/completion, timing, EOF, cancellation and stream ownership passed");
}
