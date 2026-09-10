#include "media_io.h"
#include "files.h"
#include "load_timing.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <libavutil/mem.h>
#include <psp2/kernel/processmgr.h>

static int read_packet(void *opaque,uint8_t *buffer,int size) {
    HostMediaInput *input=opaque;
    if(input->position>=input->size)return AVERROR_EOF;
    uint64_t started=sceKernelGetProcessTimeWide();
    int n=host_stream_read(input->reader,buffer,size,input->position);
    uint64_t elapsed=sceKernelGetProcessTimeWide()-started;
    input->read_calls++;input->read_us+=elapsed;
    if(elapsed>input->max_read_us)input->max_read_us=elapsed;
    if(n>0)input->read_bytes+=n;
    if(n<0)return AVERROR(EIO);
    if(n==0)return AVERROR_EOF;
    input->position+=n;
    return n;
}
static int64_t seek_packet(void *opaque,int64_t offset,int whence) {
    HostMediaInput *input=opaque;
    if(whence==AVSEEK_SIZE)return input->size;
    whence &= ~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?input->position:whence==SEEK_END?input->size:-1;
    if(base<0 || offset < -base || offset > input->size-base)return AVERROR(EINVAL);
    return input->position=base+offset;
}
void host_media_input_close(HostMediaInput *input) {
    if(input->read_calls)av_log(NULL,AV_LOG_INFO,"[media-io] path=%s reads=%llu bytes=%llu total_us=%llu max_us=%llu\n",input->path,(unsigned long long)input->read_calls,(unsigned long long)input->read_bytes,(unsigned long long)input->read_us,(unsigned long long)input->max_read_us);
    if(input->format)avformat_close_input(&input->format);
    if(input->io){av_freep(&input->io->buffer);avio_context_free(&input->io);}
    host_stream_close(input->reader);
    memset(input,0,sizeof(*input));
}
int host_media_input_open(HostMediaInput *input,const char *path) {
    memset(input,0,sizeof(*input));
    if(!path || strlen(path)>=sizeof(input->path))return AVERROR(EINVAL);
    strcpy(input->path,path);
    input->reader=host_stream_open(path,&input->size);
    if(!input->reader)return AVERROR(ENOENT);
    unsigned char *buffer=av_malloc(32768);
    if(!buffer){host_media_input_close(input);return AVERROR(ENOMEM);}
    input->io=avio_alloc_context(buffer,32768,0,input,read_packet,NULL,seek_packet);
    if(!input->io){av_free(buffer);host_media_input_close(input);return AVERROR(ENOMEM);}
    input->format=avformat_alloc_context();
    if(!input->format){host_media_input_close(input);return AVERROR(ENOMEM);}
    input->format->pb=input->io;
    input->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    uint64_t started=host_load_clock();
    int result=avformat_open_input(&input->format,path,NULL,NULL);
    host_load_report("demux-open",path,started,started,host_load_clock(),result);
    if(result>=0){
        started=host_load_clock();result=avformat_find_stream_info(input->format,NULL);
        host_load_report("stream-info",path,started,started,host_load_clock(),result);
    }
    if(result<0)host_media_input_close(input);
    return result;
}
