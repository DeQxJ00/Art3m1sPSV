#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <unistd.h>
#include "../../host/audio.c"
struct HostReadStream {FILE *file;};
static atomic_int live, notifications, output_calls, storage_reads;
static atomic_int async_test, block_prepare, prepare_blocked;
// Model per-thread kernel state: verify priority is applied in the worker,
// before output or voice I/O, including workers recreated after shutdown.
static _Thread_local int thread_priority=159;
int sceKernelGetThreadId(void){return 1;}
int sceKernelChangeThreadPriority(int tid,int priority){assert(tid==1);thread_priority=priority;return 0;}
int sceKernelGetThreadInfo(int tid,SceKernelThreadInfo *info){assert(tid==1&&info->size==sizeof(*info));info->currentPriority=thread_priority;return 0;}
static pthread_mutex_t ledger_lock=PTHREAD_MUTEX_INITIALIZER;
static int64_t ledger_live,ledger_reserved,ledger_peak;
void art3m1s_resource_event(uint32_t region,uint32_t owner,int64_t l,int64_t r,int64_t retired){
    assert(region==0&&owner==10&&retired==0);
    pthread_mutex_lock(&ledger_lock);
    ledger_live+=l;ledger_reserved+=r;
    assert(ledger_live>=0&&ledger_reserved>=0);
    if(ledger_live+ledger_reserved>ledger_peak)ledger_peak=ledger_live+ledger_reserved;
    pthread_mutex_unlock(&ledger_lock);
}
void host_load_timing_log(const char *op,const char *path,uint64_t wait,uint64_t work,uint64_t at,int result){
    (void)op;(void)path;(void)wait;(void)work;(void)at;(void)result;
}
static int fail_read_at;
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static const void *previous;
static int16_t saved[BLOCK*2];
static int release_worker_test_mode;
HostReadStream *host_stream_open(const char *path,int64_t *size){
    if(atomic_load(&async_test))assert(thread_priority==144);
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    HostReadStream *s=malloc(sizeof(*s));s->file=f;live++;return s;
}
int host_stream_read(HostReadStream *s,uint8_t *out,int cap,int64_t pos){
    storage_reads++;
    if(fail_read_at&&--fail_read_at==0)return -1;
    if(atomic_load(&async_test)&&pthread_equal(pthread_self(),prepare_worker)&&atomic_load(&block_prepare)){
        assert(thread_priority==144);
        atomic_store(&prepare_blocked,1);
        while(atomic_load(&block_prepare))pause_ms();
    }
    if(pos<0 || fseek(s->file,pos,SEEK_SET))return -1;return fread(out,1,cap,s->file);
}
void host_stream_close(HostReadStream *s){if(s){fclose(s->file);free(s);live--;}}
void host_files_audio_playback(int enabled){(void)enabled;}
int host_read(const char *path,uint8_t *out,int cap,int64_t pos){struct stat st;return stat(path,&st)?-1:st.st_size;}
void host_video_command(const char *a,const char *b){}
void art3m1s_runtime_notify_sound_finished(void *p,const char *id){assert(!strcmp(id,"voice"));notifications++;}
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceAudioOutOpenPort(int a,int count,int rate,int mode){assert(thread_priority==128);assert(count==BLOCK&&rate==48000);return 0;}
int sceAudioOutSetVolume(int a,int b,int*c){return 0;}
int sceAudioOutReleasePort(int p){return 0;}
int sceAudioOutOutput(int p,const void *pcm){
    if(!pcm)return 0;
    if(previous){assert(previous!=pcm);assert(!memcmp(previous,saved,sizeof(saved)));}
    previous=pcm;memcpy(saved,pcm,sizeof(saved));output_calls++;
    host_audio_poll(NULL);
    if(release_worker_test_mode){
        if(output_calls==1){
            if(release_worker_test_mode==1)host_media_command("audio_voice_stop","{\"id\":\"voice\"}");
            else if(release_worker_test_mode==2)host_media_command("audio_stop_all","{}");
            else running=0; // shutdown must submit a final ramp before draining
        }else{
            assert(output_calls==2);
            assert(saved[0]>26000&&saved[0]<26214);
            assert(saved[1]<-13000&&saved[1]>-13107);
            for(int i=1;i<AUDIO_RELEASE_FRAMES;i++){
                assert(saved[2*i]<=saved[2*i-2]);
                assert(saved[2*i+1]>=saved[2*i-1]);
            }
            for(int i=2*(AUDIO_RELEASE_FRAMES-1);i<BLOCK*2;i++)assert(saved[i]==0);
            running=0;
        }
    }else if(atomic_load(&async_test))pause_ms();
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
static void direct_command(const char *kind,const char *json){
    cJSON *j=cJSON_Parse(json);assert(j);apply_command(kind,j,0);cJSON_Delete(j);
}
static void constant_track(Track *t,const char *id,int frames){
    memset(t,0,sizeof(*t));t->active=1;t->channel=3;t->available=frames;
    t->envelope.gain=1;snprintf(t->id,sizeof(t->id),"%s",id);
    for(int i=0;i<frames;i++){t->samples[2*i]=.8f;t->samples[2*i+1]=-.4f;}
}
static void release_test(void){
    direct_command("audio_set_smoothing","{\"enabled\":true}");
    float mix[BLOCK*2];Track *t=&tracks[0];
    // Both continued PCM and an exhausted cache must smoothly reach zero.
    // Use a real decoder to check that the stop frees it without extra reads.
    for(int cached=0;cached<3;cached++){
        assert(open_decoder(t,"48000-2.ogg")==0);
        strcpy(t->id,"voice");t->envelope.gain=.5f;t->available=cached==2?1:100;
        for(int i=0;i<t->available;i++){t->samples[2*i]=.8f;t->samples[2*i+1]=-.4f;}
        memset(mix,0,sizeof(mix));assert(mix_track_span(t,mix,1,.6f,.3f)==1);t->cursor++;
        // The release must follow actual cached samples, not always repeat DC.
        if(cached==1)for(int i=1;i<t->available;i++)t->samples[2*i]=.8f-i*.001f;
        int reads=storage_reads;
        direct_command("audio_voice_stop",cached==2?"{\"id\":\"voice\",\"fade_ms\":-1}":"{\"id\":\"voice\"}");
        assert(!t->active&&!live&&storage_reads==reads&&release_pending);
        assert(!decoded_finished&&!submitted_finished&&!finished);
        memset(mix,0,sizeof(mix));mix_release_tail(mix);
        for(int i=0;i<AUDIO_RELEASE_FRAMES;i++){
            int source=i+1;if(source>99)source=99;
            float l=cached==1?(.8f-source*.001f)*.3f:.24f;
            float factor=(AUDIO_RELEASE_FRAMES-1-i)/(float)AUDIO_RELEASE_FRAMES;
            assert(fabsf(mix[2*i]-l*factor)<1e-6f);
            assert(fabsf(mix[2*i+1]+.06f*factor)<1e-6f);
        }
        for(int i=AUDIO_RELEASE_FRAMES*2;i<BLOCK*2;i++)assert(mix[i]==0);
        memset(mix,0,sizeof(mix));mix_release_tail(mix);
        for(int i=0;i<BLOCK*2;i++)assert(mix[i]==0); // drain exactly once
    }
    // Never-played and pending tracks must not leak old/uninitialized PCM.
    for(int pending=0;pending<2;pending++){
        constant_track(t,"voice",1);t->preparing=pending;
        direct_command("audio_voice_stop","{\"id\":\"voice\"}");
        assert(!release_pending&&!t->active);
    }
    // Explicit long fades retain their duration and use the existing envelope.
    constant_track(t,"voice",1);
    direct_command("audio_voice_stop","{\"id\":\"voice\",\"fade_ms\":500}");
    assert(t->active&&t->envelope.left==24000&&t->envelope.stop_at_zero&&!release_pending);close_track(t);
    // A same-ID replacement (including zero-duration crossfade) needs no spare
    // slot, and its gain complements the outgoing ramp instead of doubling it.
    for(int cross=0;cross<4;cross++){
        for(int i=0;i<TRACKS;i++){constant_track(&tracks[i],i?"occupied":"",1);tracks[i].preparing=i!=0;}
        memset(mix,0,sizeof(mix));mix_track_span(t,mix,1,1,1);t->cursor++;
        if(cross==2)direct_command("audio_bgm_stop","{}");
        if(cross==3){
            // Multiple commands before the next mix must retain the ramp,
            // even if an intermediate replacement never got to emit audio.
            direct_command("audio_bgm_play","{\"file\":\"48000-2.ogg\"}");
        }
        direct_command(cross==1?"audio_bgm_crossfade":"audio_bgm_play","{\"file\":\"48000-2.ogg\"}");
        assert(t->active&&t->vorbis&&t->envelope.left==AUDIO_RELEASE_FRAMES&&release_pending);
        for(int i=0;i<TRACKS;i++)assert(tracks[i].active);
        t->cursor=0;t->available=AUDIO_RELEASE_FRAMES;
        for(int i=0;i<t->available;i++){t->samples[2*i]=.8f;t->samples[2*i+1]=-.4f;}
        memset(mix,0,sizeof(mix));mix_track_span(t,mix,AUDIO_RELEASE_FRAMES,1,1);mix_release_tail(mix);
        for(int i=0;i<AUDIO_RELEASE_FRAMES;i++){assert(fabsf(mix[2*i]-.8f)<1e-5f);assert(fabsf(mix[2*i+1]+.4f)<1e-5f);}
        for(int i=0;i<TRACKS;i++)close_track(&tracks[i]);assert(live==0);
    }
    // Explicit crossfade still keeps two decoders with its requested duration.
    constant_track(t,"",1);
    direct_command("audio_bgm_crossfade","{\"file\":\"48000-2.ogg\",\"time_ms\":250}");
    assert(t->active&&t->envelope.left==12000&&!strcmp(t->id,"__old_bgm"));
    assert(tracks[1].active&&tracks[1].envelope.left==12000&&!release_pending);
    close_track(t);close_track(&tracks[1]);assert(live==0);
    // Multiple stops add bounded tails; unrelated tracks are never attenuated.
    for(int i=0;i<2;i++){
        constant_track(&tracks[i],"voice",1);memset(mix,0,sizeof(mix));
        mix_track_span(&tracks[i],mix,1,.25f,.25f);tracks[i].cursor++;
    }
    direct_command("audio_stop_all","{}");memset(mix,0,sizeof(mix));mix[0]=.1f;mix_release_tail(mix);
    assert(fabsf(mix[0]-(.1f+.4f*(1-1.0f/AUDIO_RELEASE_FRAMES)))<1e-6f);
    assert(!release_pending&&!live);
    puts("PASS: stop ramps, cached/exhausted PCM, gain/pan preservation, no extra stop I/O, replacement at track limit, zero/explicit crossfade, stop_all.");
}
static void release_worker_test(void){
    for(int mode=1;mode<=3;mode++){
        direct_command("audio_set_smoothing","{\"enabled\":true}");
        previous=NULL;output_calls=0;notifications=0;release_worker_test_mode=mode;
        constant_track(&tracks[0],"voice",8192);running=1;
        int reads=storage_reads;audio_worker(NULL);
        assert(output_calls==2&&storage_reads==reads&&!release_pending&&!tracks[0].active);
        assert(notifications==0);host_audio_stop();assert(live==0&&ledger_live==0);
    }
    release_worker_test_mode=0;previous=NULL;output_calls=0;notifications=0;
    puts("PASS: output PCM has continuous 12 ms stop/stop_all/shutdown ramps, exact silent tail, double-buffer ownership, no storage reads or false completion.");
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
    // Looping SE also prepares off-thread; a crossfade while BGM is still
    // preparing must discard that pending track instead of orphaning it.
    previous=NULL;block_prepare=1;prepare_blocked=0;assert(!host_audio_start());
    host_media_command("audio_se_play","{\"id\":\"ambient\",\"file\":\"long.ogg\",\"loop\":true}");
    wait_counter(&prepare_blocked,1);
    at=output_calls;wait_counter(&output_calls,at+8);
    host_media_command("audio_bgm_play","{\"file\":\"48000-2.ogg\",\"loop\":true,\"loop_file\":\"fallback.wav\"}");
    host_media_command("audio_bgm_crossfade","{\"file\":\"44100-2.ogg\",\"loop\":true,\"time_ms\":250}");
    host_media_command("audio_stop_all","{}");block_prepare=0;
    at=output_calls;wait_counter(&output_calls,at+10);
    host_audio_stop();assert(live==0&&!prepare_count&&!host_vorbis_preload_bytes_in_use());
    // Restart/exit with queued work must join preparation before files close.
    previous=NULL;assert(!host_audio_start());
    host_media_command("audio_voice_play","{\"id\":\"voice\",\"file\":\"long.ogg\"}");
    host_audio_stop();assert(live==0&&!prepare_count&&!host_vorbis_preload_bytes_in_use());
    async_test=0;previous=NULL;output_calls=0;notifications=0;
    puts("PASS: background preparation, uninterrupted output, pending-stop fade, same-ID replacement, fallback, stop_all, restart/shutdown ownership.");
}
static void prepared_loop_handoff_test(void){
    const char *loop_files[]={"48000-2.ogg","44100-1.ogg","fallback.wav","padded.ogg"};
    for(int variant=0;variant<4;variant++){
        Prepared *p=host_audio_alloc(sizeof(*p),1);assert(p);
        p->decoder=host_audio_alloc(sizeof(*p->decoder),1);assert(p->decoder);
        strcpy(p->id,"handoff");strcpy(p->kind,"audio_se_play");strcpy(p->path,"48000-2.ogg");
        p->decoder->vorbis=host_vorbis_open_preloaded(p->path,&p->decoder->rate,&p->decoder->channels,NULL,NULL);
        assert(p->decoder->vorbis);
        assert(!open_decoder(p->decoder,p->path));strcpy(p->decoder->loop_path,loop_files[variant]);
        Track *t=&tracks[0];assert(!t->active);
        strcpy(t->id,p->id);t->active=t->preparing=t->loop=1;
        t->pan=.4f;t->envelope.gain=.6f;
        Generation *g=generation_for(p->id);p->generation=t->generation=g->value=++sequence;
        running=1;p->loop=1;prepare_loop(p);
        assert(p->decoder->loop_prepared==1&&p->decoder->prepared_loop);
        Track *next=p->decoder->prepared_loop;
        const int primed=next->available;assert(primed>0);
        float *prefix=malloc(primed*2*sizeof(float));memcpy(prefix,next->samples,primed*2*sizeof(float));
        ready_first=ready_last=p;prepare_count=1;publish_prepared();
        assert(t->active&&!t->preparing&&t->loop&&t->prepared_loop);
        assert(t->pan==.4f&&t->envelope.gain==.6f&&!prepare_count);
        int frames=0,transition=0;
        while(frames<30000){
            const int pending=t->loop_path[0]!=0,reads=storage_reads;
            int n=decode(t);assert(n>0);
            if(pending&&!t->loop_path[0]){
                assert(frames==11040&&n==primed&&storage_reads==reads);
                assert(!memcmp(prefix,t->samples,n*2*sizeof(float)));transition++;
                assert(t->pan==.4f&&t->envelope.gain==.6f);
            }
            frames+=n;
        }
        assert(transition==1);free(prefix);close_track(t);host_audio_stop();
        assert(live==0&&!host_vorbis_preload_bytes_in_use());
    }
    // Cancellation releases both decoders and the loop's cache reservation.
    Prepared *p=host_audio_alloc(sizeof(*p),1);assert(p);
    p->decoder=host_audio_alloc(sizeof(*p->decoder),1);assert(p->decoder);
    strcpy(p->id,"cancel-loop");strcpy(p->decoder->loop_path,"long.ogg");
    Generation *g=generation_for(p->id);p->generation=g->value=++sequence;p->loop=1;running=1;
    prepare_loop(p);assert(p->decoder->prepared_loop&&host_vorbis_preload_bytes_in_use());
    g->value=++sequence;assert(prepare_cancelled(p));prepare_count=1;free_prepared(p);host_audio_stop();
    assert(live==0&&!prepare_count&&!host_vorbis_preload_bytes_in_use());
    puts("PASS: primed Ogg/resampled/WAV/streaming loop handoff performs no storage reads, keeps PCM prefix and gain/pan, and frees cancelled pairs.");
}

static void smoothing_policy_test(void){
    assert(!smoothing_enabled); // absent settings default to immediate stop/play
    float mix[BLOCK*2]={0};Track *t=&tracks[0];
    for(int all=0;all<2;all++){
        constant_track(t,"voice",1);mix_track_span(t,mix,1,1,1);t->cursor++;
        int reads=storage_reads;
        direct_command(all?"audio_stop_all":"audio_voice_stop","{\"id\":\"voice\"}");
        assert(!t->active&&!release_pending&&storage_reads==reads);
    }
    constant_track(t,"",1);mix_track_span(t,mix,1,1,1);
    direct_command("audio_bgm_play","{\"file\":\"48000-2.ogg\"}");
    assert(t->active&&t->envelope.left==0&&t->envelope.gain==1&&!release_pending);
    direct_command("audio_bgm_stop","{\"fade_ms\":500}");
    assert(t->active&&t->envelope.left==24000&&t->envelope.stop_at_zero);
    direct_command("audio_bgm_crossfade","{\"file\":\"48000-2.ogg\",\"time_ms\":250}");
    assert(t->active&&t->envelope.left==12000&&!strcmp(t->id,"__old_bgm"));
    assert(tracks[1].active&&tracks[1].envelope.left==12000&&!release_pending);
    close_track(t);close_track(&tracks[1]);
    direct_command("audio_set_smoothing","{\"enabled\":true}");
    constant_track(t,"voice",1);mix_track_span(t,mix,1,1,1);
    direct_command("audio_voice_stop","{\"id\":\"voice\"}");assert(release_pending&&released_count);
    direct_command("audio_set_smoothing","{\"enabled\":false}");
    memset(mix,0,sizeof(mix));mix_release_tail(mix);
    for(int i=0;i<BLOCK*2;i++)assert(mix[i]==0);
    assert(!release_pending&&!released_count&&!live);
    puts("PASS: smoothing defaults off, immediate stop/replacement, explicit fades preserved, disabling clears pending ramps.");
}
int main(void){
    mixer_test();smoothing_policy_test();release_test();release_worker_test();
    for(int p=0;p<2;p++){decode_test("48000-1.ogg",p);decode_test("48000-2.ogg",p);decode_test("44100-1.ogg",p);decode_test("44100-2.ogg",p);}
    preload_test();async_prepare_test();prepared_loop_handoff_test();
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
    Finished *stale=host_audio_alloc(sizeof(*stale),1);assert(stale);strcpy(stale->id,"voice");stale->generation=old;finished=stale;host_audio_poll(NULL);assert(notifications==1);host_audio_stop();
    puts("PASS: fades/pan/span boundaries/clipping, resampling, EOF/loop, mixed decoder intro-loop, double-buffer ownership, deferred completion, stale generation suppression, no stream leaks.");
    assert(ledger_live==0&&ledger_reserved==0&&ledger_peak>=HOST_VORBIS_PRELOAD_TOTAL_LIMIT);
    printf("PASS audio ledger: live=%lld reserved=%lld peak=%lld after stop/replacement/fallback/shutdown\n",(long long)ledger_live,(long long)ledger_reserved,(long long)ledger_peak);
}
