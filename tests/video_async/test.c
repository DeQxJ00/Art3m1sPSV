#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include "../../host/video.c"
struct HostReadStream {FILE *file;};
static pthread_t main_thread;
static int live,notifications,uploads;
static unsigned expected_width=64,expected_height=64;
static int64_t last_presented;
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
HostReadStream *host_stream_open(const char *path,int64_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t offset){if(fseek(s->file,offset,SEEK_SET))return -1;return fread(out,1,cap,s->file);}
void host_stream_close(HostReadStream *s){if(s){fclose(s->file);free(s);live--;}}
int host_read(const char *path,uint8_t *out,int cap,int64_t offset){struct stat st;(void)out;(void)cap;(void)offset;return stat(path,&st)?-1:(int)st.st_size;}
void host_media_command(const char *kind,const char *json){(void)kind;(void)json;assert(pthread_equal(main_thread,pthread_self()));}
void art3m1s_runtime_notify_video_finished(void *runtime,const char *name){assert(runtime==(void*)1);assert(!strcmp(name,"arrow"));assert(pthread_equal(main_thread,pthread_self()));notifications++;}
int art3m1s_runtime_upload_video_layer_frame(void *runtime,const char *name,unsigned w,unsigned h,const unsigned char *p,size_t size){
    assert(pthread_equal(main_thread,pthread_self()));assert(runtime==(void*)1);assert(!strcmp(name,"arrow"));
    assert(w==expected_width&&h==expected_height&&size==w*h*4);assert(p[3]>=100&&p[3]<=150);
    uploads++;last_presented=sceKernelGetProcessTimeWide();return 1;
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
    if(argc>=4&&!strcmp(argv[3],"loop")) {
        host_video_command("video_layer_play","{\"id\":\"arrow\",\"file\":\"arrow.ogv\",\"loop\":true}");
        uint64_t start=sceKernelGetProcessTimeWide();int64_t previous=0;
        while(async_last_pts<1200000) {
            host_video_tick((void*)1);assert(async_mode&&!notifications);
            assert(async_last_pts>=previous);previous=async_last_pts;
            assert(sceKernelGetProcessTimeWide()-start<4000000);
            struct timespec pause={0,1000000};nanosleep(&pause,NULL);
        }
        assert(uploads>=30);host_video_close();assert(!live&&!async_mode);
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
