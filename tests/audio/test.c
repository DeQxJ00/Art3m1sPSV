#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include "../../host/audio.c"
struct HostReadStream {FILE *file;};
static int live, notifications, output_calls, storage_reads;
static const void *previous;
static int16_t saved[BLOCK*2];
HostReadStream *host_stream_open(const char *path,int64_t *size){
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t pos){
    storage_reads++;
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
    if(output_calls<=7)assert(notifications==0);
    if(output_calls==12)running=0;
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
static void decode_test(const char *name){
    Track *t=calloc(1,sizeof(*t));assert(open_decoder(t,name)==0&&t->vorbis);
    char refname[128];snprintf(refname,sizeof(refname),"%.*s.pcm",(int)strlen(name)-4,name);
    FILE *ref=fopen(refname,"rb");assert(ref);int total=0,compared=0,tail=0;double squared=0;float max=0;
    int n;while((n=decode(t))>0){
        for(int i=0;i<n*2;i++){float expected;if(fread(&expected,4,1,ref)!=1){tail++;continue;}compared++;float error=fabsf(t->samples[i]-expected);squared+=error*error;if(error>max)max=error;}
        total+=n;
    }
    fprintf(stderr,"comparison %s: total=%d tail_samples=%d rms=%g max=%g\n",name,total,tail,sqrt(squared/compared),max);
    assert(n==0);assert(total==11040);assert(tail<=256*2);assert(sqrt(squared/compared)<.0001&&max<.002);
    printf("PASS %s: %d frames, PCM RMS error %.8f max %.8f\n",name,total,sqrt(squared/(total*2)),max);
    fclose(ref);t->loop=1;assert(decode(t)>0);close_track(t);free(t);assert(live==0);
}
int main(void){
    mixer_test();decode_test("48000-1.ogg");decode_test("48000-2.ogg");decode_test("44100-1.ogg");decode_test("44100-2.ogg");
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
