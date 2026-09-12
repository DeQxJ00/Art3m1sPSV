#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <string.h>

typedef struct { AVFormatContext *f; AVCodecContext *c; AVPacket *p; AVFrame *v; int stream,drain; } Reader;
static int open_reader(Reader *r,const char *path,int gray){
    const AVCodec *codec=NULL;int e=avformat_open_input(&r->f,path,NULL,NULL);if(e<0)return e;
    if((e=avformat_find_stream_info(r->f,NULL))<0)return e;
    if((e=av_find_best_stream(r->f,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0))<0)return e;
    r->stream=e;r->c=avcodec_alloc_context3(codec);r->p=av_packet_alloc();r->v=av_frame_alloc();
    if(!r->c||!r->p||!r->v)return AVERROR(ENOMEM);
    if((e=avcodec_parameters_to_context(r->c,r->f->streams[e]->codecpar))<0)return e;
    r->c->thread_count=1;if(gray)r->c->flags|=AV_CODEC_FLAG_GRAY;
    return avcodec_open2(r->c,codec,NULL);
}
static int next(Reader *r){
    av_frame_unref(r->v);
    for(;;){
        int e=avcodec_receive_frame(r->c,r->v);if(e!=AVERROR(EAGAIN))return e;
        if(r->drain)return AVERROR_INVALIDDATA;
        do{av_packet_unref(r->p);e=av_read_frame(r->f,r->p);}while(e>=0&&r->p->stream_index!=r->stream);
        if(e<0){if(e!=AVERROR_EOF)return e;r->drain=1;e=avcodec_send_packet(r->c,NULL);}
        else e=avcodec_send_packet(r->c,r->p);
        av_packet_unref(r->p);if(e<0)return e;
    }
}
static void close_reader(Reader *r){av_frame_free(&r->v);av_packet_free(&r->p);avcodec_free_context(&r->c);avformat_close_input(&r->f);}
int host_mask_gray_probe(const char *path,const char *report){
    FILE *log=fopen(report,"w");if(!log)return 2;
    Reader a={0},b={0};int status=1,frames=0,e=open_reader(&a,path,0),g=0;
    if(e<0){fprintf(log,"normal open error=%d\n",e);goto done;}
    e=open_reader(&b,path,1);if(e<0){fprintf(log,"gray open error=%d\n",e);goto done;}
    for(;;){
        e=next(&a);g=next(&b);
        if(e==AVERROR_EOF&&g==AVERROR_EOF){status=frames>0?0:1;break;}
        if(e<0||g<0){fprintf(log,"decode error normal=%d gray=%d\n",e,g);break;}
        if(a.v->width!=b.v->width||a.v->height!=b.v->height||a.v->best_effort_timestamp!=b.v->best_effort_timestamp){fprintf(log,"frame/PTS mismatch\n");break;}
        int same=1;
        for(int y=0;y<a.v->height;y++)if(memcmp(a.v->data[0]+(ptrdiff_t)y*a.v->linesize[0],b.v->data[0]+(ptrdiff_t)y*b.v->linesize[0],a.v->width)){same=0;break;}
        if(!same){fprintf(log,"Y mismatch at frame=%d\n",frames);break;}
        frames++;
    }
done:
    fprintf(log,"%s frames=%d; normal versus GRAY Y and PTS exact; source=%s\n",status?"FAIL":"PASS",frames,path);
    close_reader(&a);close_reader(&b);fclose(log);return status;
}
#ifdef MASK_PROBE_STANDALONE
int main(int argc,char **argv){if(argc!=3)return 2;return host_mask_gray_probe(argv[1],argv[2]);}
#endif
