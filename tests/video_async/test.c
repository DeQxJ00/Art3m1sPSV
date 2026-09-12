#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <libavcodec/avcodec.h>
static int slow_decode;
static int test_receive_frame(AVCodecContext *,AVFrame *);
#define avcodec_receive_frame test_receive_frame
#include "../../host/video.c"
#undef avcodec_receive_frame
static int test_receive_frame(AVCodecContext *c,AVFrame *f){
    int r=avcodec_receive_frame(c,f);
    if(!r&&c==decoder&&slow_decode){struct timespec pause={0,80000000};nanosleep(&pause,NULL);}
    return r;
}
struct HostReadStream {FILE *file;};
static pthread_t main_thread;
static int live,notifications,uploads,stream_reads,fail_next_read;
static int test_yuva,yuva_uploads;
static unsigned expected_width=64,expected_height=64;
static int64_t last_presented;
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
HostReadStream *host_stream_open(const char *path,int64_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t offset){stream_reads++;if(fail_next_read){fail_next_read=0;return -1;}if(fseek(s->file,offset,SEEK_SET))return -1;return fread(out,1,cap,s->file);}
void host_stream_close(HostReadStream *s){if(s){fclose(s->file);free(s);live--;}}
int host_read(const char *path,uint8_t *out,int cap,int64_t offset){struct stat st;(void)out;(void)cap;(void)offset;return stat(path,&st)?-1:(int)st.st_size;}
void host_media_command(const char *kind,const char *json){(void)kind;(void)json;assert(pthread_equal(main_thread,pthread_self()));}
void art3m1s_runtime_notify_video_finished(void *runtime,const char *name){assert(runtime==(void*)1);assert(!strcmp(name,"arrow"));assert(pthread_equal(main_thread,pthread_self()));notifications++;}
int art3m1s_runtime_upload_video_layer_frame(void *runtime,const char *name,unsigned w,unsigned h,const unsigned char *p,size_t size){
    assert(pthread_equal(main_thread,pthread_self()));assert(runtime==(void*)1);assert(!strcmp(name,"arrow"));
    assert(w==expected_width&&h==expected_height&&size==w*h*4);assert(p[3]>=100&&p[3]<=150);
    uploads++;last_presented=sceKernelGetProcessTimeWide();return 1;
}
int host_gxm_video_yuva_available(void){assert(pthread_equal(main_thread,pthread_self()));return test_yuva;}
void host_gxm_video_yuva_close(void){assert(pthread_equal(main_thread,pthread_self()));}
int host_gxm_video_yuva_upload(void *runtime,const char *name,unsigned w,unsigned h,const uint8_t *p){
    assert(test_yuva&&pthread_equal(main_thread,pthread_self()));
    size_t n=(size_t)w*h;uint8_t *converted=malloc(n*4),*a=malloc(n);assert(converted&&a);
    for(unsigned y=0;y<h;y++){
        host_video_gray_row(p+3*n+y*w,a+y*w,w);
        host_video_yuv444_row(p+y*w,p+n+y*w,p+2*n+y*w,a+y*w,converted+4*y*w,w);
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
unsigned host_gxm_video_rgba(unsigned x,int w,int h,const uint8_t*p){(void)x;(void)w;(void)h;(void)p;assert(0);return 0;}
void host_gxm_video_delete(unsigned x){(void)x;assert(pthread_equal(main_thread,pthread_self()));}
void host_gxm_video_draw(unsigned x,float u,float v){(void)x;(void)u;(void)v;}
int main(int argc,char **argv){
    if(argc>=3){expected_width=atoi(argv[1]);expected_height=atoi(argv[2]);}
    int stall=argc>=4;
    main_thread=pthread_self();
    test_yuva=argc>=4&&!strcmp(argv[3],"yuva");
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
        width=expected_width;height=expected_height;
        size_t bytes=(size_t)width*height;
        uint8_t *reference=malloc(bytes*15);assert(reference);
        assert(open_mask("arrow.ogv")==0&&mask_decoder);
        for(int i=0;i<15;i++){
            assert(mask_at_time((int64_t)i*1000000/30,1)==0);
            memcpy(reference+bytes*i,mask_pixels,bytes);
        }
        close_mask();assert(!live);
        assert(memcmp(reference,reference+14*bytes,bytes)!=0);
        assert(open_mask("arrow.ogv")==0&&mask_decoder);
        for(int i=0;i<15;i++){
            assert(mask_at_time((int64_t)i*1000000/30,0)==0);
            if(i%3==2){
                assert(mask_at_time((int64_t)i*1000000/30,1)==0);
                assert(!memcmp(reference+bytes*i,mask_pixels,bytes));
            }
        }
        // EOF keeps the last converted alpha; no stale or uninitialized frame.
        assert(mask_at_time(1000000,1)==0);
        assert(!memcmp(reference+14*bytes,mask_pixels,bytes));
        close_mask();free(reference);assert(!live);
        puts("Deferred mask conversion: advancing skipped pairs preserves selected alpha and EOF");
        return 0;
    }
    if(argc>=4&&(!strcmp(argv[3],"loop")||!strcmp(argv[3],"slow-codec")||test_yuva)) {
        slow_decode=!strcmp(argv[3],"slow-codec");
        host_video_command("video_layer_play","{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"loop\":true}");
        uint64_t start=sceKernelGetProcessTimeWide();int64_t previous=0;int initial_reads=-1;
        while(async_last_pts<1200000) {
            host_video_tick((void*)1);assert(async_mode&&!notifications);
            if(initial_reads<0)initial_reads=stream_reads;
            assert(stream_reads==initial_reads&&input.cached&&mask_input.cached);
            assert(async_last_pts>=previous);previous=async_last_pts;
            assert(sceKernelGetProcessTimeWide()-start<4000000);
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        }
        if(slow_decode)fprintf(stderr,"Slow decoder: uploads=%d skipped=%u\n",uploads,async_skipped_conversion);
        assert(uploads>=30);host_video_close();assert(!live&&!async_mode);
        if(test_yuva)assert(yuva_uploads==uploads&&yuva_uploads>=30);
        puts("Looping Theora+mask: monotonic PTS across >2 loops, no EOF completion, cancellation passed");
        return 0;
    }
    const char *command="{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"skippable\":true}";
    host_video_command("video_layer_play",command);
    uint64_t start=sceKernelGetProcessTimeWide();int ticks=0;
    while(!notifications){host_video_tick((void*)1);ticks++;assert(sceKernelGetProcessTimeWide()-start<5000000);struct timespec pause={0,1000000};
        if(stall&&uploads==1){pause.tv_nsec=300000000;stall=0;}
        nanosleep(&pause,NULL);}
    assert(uploads>0&&uploads<=15);if(argc<4)assert(uploads>=10);
    else assert(async_skipped_conversion>0);
    assert(ticks>50);assert(!live);assert(!async_mode);
    assert(sceKernelGetProcessTimeWide()-(uint64_t)last_presented>=15000);
    // Exercise shutdown while a producer is blocked on a full frame queue.
    for(int i=0;i<20;i++) {
        host_video_command("video_layer_play",command);host_video_tick((void*)1);
        assert(async_mode);struct timespec pause={0,20000000};nanosleep(&pause,NULL);
        host_video_close();assert(!live&&!async_mode);
    }
    assert(notifications==1);
    puts("Theora+mask: main-thread-only upload/completion, timing, EOF, cancellation and stream ownership passed");
}
