#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../../host/audio.c"
struct HostReadStream {FILE *file;};
static atomic_int live, notifications, output_calls, storage_reads;
static atomic_int async_test, block_prepare, prepare_blocked;
static int fail_read_at;
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static const void *previous;
static int16_t saved[BLOCK*2];
HostReadStream *host_stream_open(const char *path,int64_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t pos){
    storage_reads++;
    if(fail_read_at&&--fail_read_at==0)return -1;
    if(atomic_load(&async_test)&&pthread_equal(pthread_self(),prepare_worker)&&atomic_load(&block_prepare)){
        atomic_store(&prepare_blocked,1);
        while(atomic_load(&block_prepare))pause_ms();
    }
    if(pos<0 || fseek(s->file,pos,SEEK_SET))return -1;return fread(out,1,cap,s->file);
}
void host_stream_close(HostReadStream *s){if(s){fclose(s->file);free(s);live--;}}
int host_read(const char *path,uint8_t *out,int cap,int64_t pos){struct stat st;return stat(path,&st)?-1:st.st_size;}
void host_video_command(const char *a,const char *b){}
void art3m1s_runtime_notify_sound_finished(void *p,const char *id){assert(!strcmp(id,"voice"));notifications++;}
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceAudioOutOpenPort(int a,int count,int rate,int mode){assert(count==BLOCK&&rate==48000);return 0;}
int sceAudioOutSetVolume(int a,int b,int*c){return 0;}
int sceAudioOutReleasePort(int p){return 0;}
int sceAudioOutOutput(int p,const void *pcm){
    if(!pcm)return 0;
    if(previous){assert(previous!=pcm);assert(!memcmp(previous,saved,sizeof(saved)));}
    previous=pcm;memcpy(saved,pcm,sizeof(saved));output_calls++;
    host_audio_poll(NULL);
    if(atomic_load(&async_test))pause_ms();
    else{
        if(output_calls<=7)assert(notifications==0);
        if(output_calls==12)running=0;
    }
    return 0;
}
static void mixer_test(void){
    float src[1000],expected[1000],actual[1000];
    for(int i=0;i<1000;i++)src[i]=sinf(i*0.73f);
    for(int f=0;f<4;f++)for(int pan=-1;pan<=1;pan++){
        HostAudioEnvelope e={.gain=.1f,.target=f==3?0:.8f,.left=f?137:0,.stop_at_zero=f==3};
        e.step=(e.target-e.gain)/137;HostAudioEnvelope ref=e;
        memset(expected,0,sizeof(expected));memset(actual,0,sizeof(actual));
        float l=.56f*(pan>0?1-pan:1),r=.56f*(pan<0?1+pan:1);int count=0;
        for(;count<500;count++){
            if(ref.left>0){ref.gain+=ref.step;if(--ref.left==0)ref.gain=ref.target;}
            if(ref.stop_at_zero&&ref.gain<=0)break;
            expected[2*count]=src[2*count]*ref.gain*l;expected[2*count+1]=src[2*count+1]*ref.gain*r;
        }
        int got=0;while(got<500){int n=500-got;if(n>71)n=71;int k=host_audio_mix(actual+2*got,src+2*got,n,&e,l,r);got+=k;if(k<n)break;}
        assert(got==count);for(int i=0;i<1000;i++)assert(fabsf(expected[i]-actual[i])<1e-6f);
    }
    float peak_samples[]={-2,-1,0,.5f,1,2};int16_t packed[6];unsigned clipped=0;
    assert(host_audio_pack(packed,peak_samples,6,&clipped)==2&&clipped==2);
    assert(packed[0]==-32767&&packed[5]==32767&&packed[3]==16383);
}
static void decode_test(const char *name,int preloaded){
    Track *t=calloc(1,sizeof(*t));
    if(preloaded){t->vorbis=host_vorbis_open_preloaded(name,&t->rate,&t->channels,NULL,NULL);assert(t->vorbis&&host_vorbis_preloaded_bytes(t->vorbis));}
    assert(open_decoder(t,name)==0&&t->vorbis);
    int reads=storage_reads;
    char refname[128];snprintf(refname,sizeof(refname),"%.*s.pcm",(int)strlen(name)-4,name);
    FILE *ref=fopen(refname,"rb");assert(ref);int total=0,compared=0,tail=0;double squared=0;float max=0;
    int n;while((n=decode(t))>0){
        for(int i=0;i<n*2;i++){float expected;if(fread(&expected,4,1,ref)!=1){tail++;continue;}compared++;float error=fabsf(t->samples[i]-expected);squared+=error*error;if(error>max)max=error;}
        total+=n;
    }
    fprintf(stderr,"comparison %s: total=%d tail_samples=%d rms=%g max=%g\n",name,total,tail,sqrt(squared/compared),max);
    assert(n==0);assert(total==11040);assert(tail<=256*2);assert(sqrt(squared/compared)<.0001&&max<.002);
    printf("PASS %s: %d frames, PCM RMS error %.8f max %.8f\n",name,total,sqrt(squared/(total*2)),max);
    fclose(ref);t->loop=1;assert(decode(t)>0);
    if(preloaded)assert(storage_reads==reads);
    close_track(t);free(t);assert(live==0);assert(!host_vorbis_preload_bytes_in_use());
}
static int cancel_after(void *p){return --*(int *)p<=0;}
static void preload_test(void){
    int rate,ch;
    HostVorbis *stream=host_vorbis_open("long.ogg",&rate,&ch);
    HostVorbis *memory=host_vorbis_open_preloaded("long.ogg",&rate,&ch,NULL,NULL);
    assert(stream&&memory&&host_vorbis_preloaded_bytes(memory));
    int16_t left[2048],right[2048];int a,b;
    do{
        a=host_vorbis_read(stream,left,1024);int reads=storage_reads;
        b=host_vorbis_read(memory,right,1024);assert(storage_reads==reads);
        assert(a==b&&a>=0&&!memcmp(left,right,(size_t)a*ch*2));
    }while(a);
    host_vorbis_close(stream);host_vorbis_close(memory);
    // A short/erroring preload must discard the partial memory copy before
    // reopening its normal stream; it must never publish truncated audio.
    fail_read_at=3;memory=host_vorbis_open_preloaded("long.ogg",&rate,&ch,NULL,NULL);
    assert(memory&&!host_vorbis_preloaded_bytes(memory));assert(host_vorbis_read(memory,left,1024)>0);host_vorbis_close(memory);
    // Valid Ogg followed by padding exercises the exact byte budget, without
    // inventing a second decoder or replacing production allocation logic.
    FILE *src=fopen("48000-2.ogg","rb"),*dst=fopen("padded.ogg","wb");assert(src&&dst);
    int c;while((c=fgetc(src))!=EOF)fputc(c,dst);fclose(src);fflush(dst);
    assert(!ftruncate(fileno(dst),HOST_VORBIS_PRELOAD_FILE_LIMIT));fclose(dst);
    HostVorbis *held[HOST_VORBIS_PRELOAD_TOTAL_LIMIT/HOST_VORBIS_PRELOAD_FILE_LIMIT];
    for(unsigned i=0;i<sizeof(held)/sizeof(*held);i++){
        held[i]=host_vorbis_open_preloaded("padded.ogg",&rate,&ch,NULL,NULL);
        assert(held[i]&&host_vorbis_preloaded_bytes(held[i])==HOST_VORBIS_PRELOAD_FILE_LIMIT);
    }
    assert(host_vorbis_preload_bytes_in_use()==HOST_VORBIS_PRELOAD_TOTAL_LIMIT);
    HostVorbis *v=host_vorbis_open_preloaded("48000-2.ogg",&rate,&ch,NULL,NULL);assert(v&&!host_vorbis_preloaded_bytes(v));
    int16_t pcm[2048];assert(host_vorbis_read(v,pcm,1024)>0);host_vorbis_close(v);
    for(unsigned i=0;i<sizeof(held)/sizeof(*held);i++)host_vorbis_close(held[i]);
    dst=fopen("padded.ogg","ab");assert(dst);fputc(0,dst);fclose(dst);
    v=host_vorbis_open_preloaded("padded.ogg",&rate,&ch,NULL,NULL);assert(v&&!host_vorbis_preloaded_bytes(v));
    assert(host_vorbis_read(v,pcm,1024)>0);host_vorbis_close(v);
    int cancel=5;assert(!host_vorbis_open_preloaded("long.ogg",&rate,&ch,cancel_after,&cancel));
    assert(!host_vorbis_open_preloaded("fallback.wav",&rate,&ch,NULL,NULL));
    assert(!host_vorbis_open_preloaded("missing",&rate,&ch,NULL,NULL));
    assert(!host_vorbis_preload_bytes_in_use()&&live==0);
    puts("PASS: preloaded PCM identity/no playback I/O, exact shared/file budgets, streaming fallback, cancellation releases reservation.");
}
static void wait_counter(atomic_int *value,int minimum){
    for(int i=0;i<5000&&atomic_load(value)<minimum;i++)pause_ms();
    assert(atomic_load(value)>=minimum);
}
static void async_prepare_test(void){
    previous=NULL;output_calls=0;notifications=0;async_test=1;block_prepare=1;prepare_blocked=0;
    assert(!host_audio_start());
    host_media_command("audio_bgm_play","{\"file\":\"48000-2.ogg\",\"loop\":true}");
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"long.ogg\"}");
    wait_counter(&prepare_blocked,1);
    int at=output_calls;wait_counter(&output_calls,at+8); // blocked storage must not stall output
    host_media_command("audio_voice_stop","{\"id\":\"voice\",\"fade_ms\":500}");
    at=output_calls;wait_counter(&output_calls,at+2);block_prepare=0;
    at=output_calls;wait_counter(&output_calls,at+10);assert(notifications==0);
    // Same-ID replacement must discard the old ready/in-flight result.
    block_prepare=1;prepare_blocked=0;
    host_media_command("audio_se_play","{\"id\":\"voice\",\"file\":\"long.ogg\"}");
    wait_counter(&prepare_blocked,1);
    host_media_command("audio_se_play","{\"id\":\"voice\",\"file\":\"48000-2.ogg\"}");
    block_prepare=0;wait_counter(&notifications,1);assert(notifications==1);
    // FFmpeg-only sources still finish through the fallback path.
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"fallback.wav\"}");
    wait_counter(&notifications,2);
    // stop_all invalidates queued preparation, even before output consumes it.
    block_prepare=1;prepare_blocked=0;
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"long.ogg\"}");
    wait_counter(&prepare_blocked,1);host_media_command("audio_stop_all","{}");block_prepare=0;
    at=output_calls;wait_counter(&output_calls,at+10);assert(notifications==2);
    host_audio_stop();assert(live==0&&!prepare_count&&!host_vorbis_preload_bytes_in_use());
    // Restart/exit with queued work must join preparation before files close.
    previous=NULL;assert(!host_audio_start());
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"long.ogg\"}");
    host_audio_stop();assert(live==0&&!prepare_count&&!host_vorbis_preload_bytes_in_use());
    async_test=0;previous=NULL;output_calls=0;notifications=0;
    puts("PASS: background preparation, uninterrupted output, pending-stop fade, same-ID replacement, fallback, stop_all, restart/shutdown ownership.");
}
int main(void){
    mixer_test();
    for(int p=0;p<2;p++){decode_test("48000-1.ogg",p);decode_test("48000-2.ogg",p);decode_test("44100-1.ogg",p);decode_test("44100-2.ogg",p);}
    preload_test();async_prepare_test();
    {
        int rate,channels;struct stat st;assert(!stat("long.ogg",&st)&&st.st_size>65536);
        HostVorbis *v=host_vorbis_open("long.ogg",&rate,&channels);assert(v&&channels==2);
        int reads=storage_reads,total=0,n;int16_t pcm[2048],prefix[256];
        assert(host_vorbis_read(v,prefix,128)==128);total=128;
        while((n=host_vorbis_read(v,pcm,1024))>0)total+=n;
        assert(n==0&&total==576000);
        assert(storage_reads-reads<=st.st_size/32768+8);
        printf("PASS long Ogg: %d frames, %d storage reads for %lld bytes; rewind preserves PCM\n",total,storage_reads-reads,(long long)st.st_size);
        assert(!host_vorbis_rewind(v));assert(host_vorbis_read(v,pcm,128)==128);assert(!memcmp(pcm,prefix,sizeof(prefix)));
        host_vorbis_close(v);assert(live==0);
    }
    int rate,ch;assert(!host_vorbis_open("fallback.wav",&rate,&ch));assert(!host_vorbis_open("missing",&rate,&ch));assert(live==0);
    // Intro -> loop can change decoder type in either direction.
    for(int reverse=0;reverse<2;reverse++){
        Track *t=calloc(1,sizeof(*t));assert(open_decoder(t,reverse?"fallback.wav":"48000-2.ogg")==0);t->loop=1;
        strcpy(t->loop_path,reverse?"48000-2.ogg":"fallback.wav");
        for(int i=0;i<40;i++)assert(decode(t)>0);
        assert(!t->loop_path[0]);close_track(t);free(t);assert(live==0);
    }
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"48000-2.ogg\"}");running=1;audio_worker(NULL);host_audio_poll(NULL);assert(notifications==1);host_audio_stop();assert(live==0);
    // A replaced/stopped voice must not deliver its old deferred completion.
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"48000-2.ogg\"}");
    uint64_t old=generations->value;
    host_media_command("audio_voice_stop","{\"id\":\"voice\"}");
    Finished *stale=calloc(1,sizeof(*stale));strcpy(stale->id,"voice");stale->generation=old;finished=stale;host_audio_poll(NULL);assert(notifications==1);host_audio_stop();
    puts("PASS: fades/pan/span boundaries/clipping, resampling, EOF/loop, mixed decoder intro-loop, double-buffer ownership, deferred completion, stale generation suppression, no stream leaks.");
}
