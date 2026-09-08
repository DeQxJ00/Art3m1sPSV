#include "audio_vorbis.h"
#include "files.h"
#include <ivorbisfile.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>

static pthread_mutex_t preload_mutex=PTHREAD_MUTEX_INITIALIZER;
static size_t preload_bytes;

struct HostVorbis {
    HostReadStream *reader;
    int64_t size, position;
    OggVorbis_File file;
    int opened, channels;
    int64_t cache_offset;
    int cache_size;
    unsigned char cache[32768];
    unsigned char *compressed;
    size_t reserved;
    HostAudioCancelled cancelled;
    void *cancel_context;
};
size_t host_vorbis_preloaded_bytes(const HostVorbis *v){return v?v->reserved:0;}
size_t host_vorbis_preload_bytes_in_use(void){
    pthread_mutex_lock(&preload_mutex);size_t n=preload_bytes;pthread_mutex_unlock(&preload_mutex);return n;
}
static int cancelled(HostVorbis *v){return v->cancelled&&v->cancelled(v->cancel_context);}
static void release_compressed(HostVorbis *v){
    free(v->compressed);v->compressed=NULL;
    pthread_mutex_lock(&preload_mutex);preload_bytes-=v->reserved;v->reserved=0;pthread_mutex_unlock(&preload_mutex);
}
static void preload(HostVorbis *v){
    if(v->size<=0 || v->size>HOST_VORBIS_PRELOAD_FILE_LIMIT)return;
    pthread_mutex_lock(&preload_mutex);
    if((size_t)v->size<=HOST_VORBIS_PRELOAD_TOTAL_LIMIT-preload_bytes){v->reserved=(size_t)v->size;preload_bytes+=v->reserved;}
    pthread_mutex_unlock(&preload_mutex);
    if(!v->reserved)return;
    v->compressed=malloc(v->reserved);
    if(!v->compressed){release_compressed(v);return;}
    for(size_t at=0;at<v->reserved;){
        if(cancelled(v)){release_compressed(v);return;}
        size_t count=v->reserved-at;if(count>sizeof(v->cache))count=sizeof(v->cache);
        int n=host_stream_read(v->reader,v->compressed+at,(int)count,(int64_t)at);
        if(n<=0 || (size_t)n>count){release_compressed(v);return;}
        at+=(size_t)n;
    }
}
static size_t read_pcm_source(void *ptr,size_t size,size_t count,void *opaque){
    HostVorbis *v=opaque;
    if(!size || !count || size>INT_MAX)return 0;
    if(count>(size_t)INT_MAX/size)count=INT_MAX/size;
    size_t wanted=size*count,done=0;
    if(cancelled(v))return 0;
    if(v->compressed){
        size_t available=(size_t)(v->size-v->position);
        if(wanted>available)wanted=available;
        memcpy(ptr,v->compressed+v->position,wanted);v->position+=(int64_t)wanted;
        return wanted/size;
    }
    while(done<wanted){
        if(cancelled(v))break;
        int64_t offset=v->position-v->cache_offset;
        if(offset<0 || offset>=v->cache_size){
            // Tremor asks for small Ogg chunks. Read ahead once instead of
            // taking the PFS reader lock and seeking the storage for each one.
            int n=host_stream_read(v->reader,v->cache,sizeof(v->cache),v->position);
            if(n<=0)break;
            v->cache_offset=v->position;v->cache_size=n;offset=0;
        }
        size_t n=v->cache_size-(size_t)offset;if(n>wanted-done)n=wanted-done;
        memcpy((unsigned char *)ptr+done,v->cache+offset,n);
        v->position+=n;done+=n;
    }
    return done/size;
}
static int seek_source(void *opaque,ogg_int64_t offset,int whence){
    HostVorbis *v=opaque;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?v->position:whence==SEEK_END?v->size:-1;
    if(base<0 || offset < -base || offset > v->size-base)return -1;
    v->position=base+offset;return 0;
}
static long tell_source(void *opaque){return (long)((HostVorbis *)opaque)->position;}
// This Tremor version calls close_func unconditionally; the adapter owns reader.
static int close_source(void *opaque){(void)opaque;return 0;}
void host_vorbis_close(HostVorbis *v){
    if(!v)return;
    if(v->opened)ov_clear(&v->file);
    host_stream_close(v->reader);release_compressed(v);free(v);
}
static HostVorbis *open_source(const char *path,int *rate,int *channels,int prepare,HostAudioCancelled check,void *context){
    HostVorbis *v=calloc(1,sizeof(*v));if(!v)return NULL;
    v->cancelled=check;v->cancel_context=context;
    if(cancelled(v))goto fail;
    v->reader=host_stream_open(path,&v->size);
    unsigned char magic[4];
    if(!v->reader || v->size>LONG_MAX || host_stream_read(v->reader,magic,4,0)!=4 || memcmp(magic,"OggS",4))goto fail;
    if(prepare)preload(v);
    if(cancelled(v))goto fail;
    ov_callbacks callbacks={read_pcm_source,seek_source,close_source,tell_source};
    if(ov_open_callbacks(v,&v->file,NULL,0,callbacks)<0)goto fail;
    v->opened=1;
    vorbis_info *info=ov_info(&v->file,-1);
    // Chained streams may change rate/layout mid-file: retain FFmpeg for those.
    if(ov_streams(&v->file)!=1 || !info || info->channels<1 || info->channels>2 || info->rate<=0)goto fail;
    if(cancelled(v))goto fail;
    *rate=info->rate;*channels=v->channels=info->channels;
    if(v->compressed){host_stream_close(v->reader);v->reader=NULL;}
    v->cancelled=NULL;v->cancel_context=NULL;return v;
fail:
    host_vorbis_close(v);return NULL;
}
HostVorbis *host_vorbis_open(const char *path,int *rate,int *channels){return open_source(path,rate,channels,0,NULL,NULL);}
HostVorbis *host_vorbis_open_preloaded(const char *path,int *rate,int *channels,HostAudioCancelled check,void *context){
    return open_source(path,rate,channels,1,check,context);
}
int host_vorbis_read(HostVorbis *v,int16_t *pcm,int frames){
    if(frames<=0 || frames>INT_MAX/(2*v->channels))return -1;
    int section=0;
    // Bound damaged-page recovery; a corrupt loop must not spin forever.
    for(int holes=0;holes<16;holes++){
        long n=ov_read(&v->file,pcm,frames*2*v->channels,&section);
        if(n==OV_HOLE)continue;
        return n<0?-1:(int)n/(2*v->channels);
    }
    return -1;
}
int host_vorbis_rewind(HostVorbis *v){return ov_pcm_seek(&v->file,0);}
