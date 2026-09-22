/* Usage: probe color.ogv mask.ogv. Read original files; never rewrite assets. */
#include "mask_decode.c"
#include "../../host/video_convert.h"
#include <libswscale/swscale.h>
#include <assert.h>
#include <stdlib.h>
int main(int argc,char **argv){
    assert(argc==3);Reader c={0},m={0};assert(open_reader(&c,argv[1],0)>=0);assert(open_reader(&m,argv[2],1)>=0);
    int w=c.c->width,h=c.c->height,n=w*h;assert(w==m.c->width&&h==m.c->height);
    uint8_t *p=calloc(1,(size_t)n*12+256);assert(p);
    uint8_t *ref=p,*out=p+4*n,*u=p+8*n,*v=p+9*n,*alpha=p+10*n,*gray=p+11*n;
    struct SwsContext *s=NULL,*g=NULL;unsigned frames=0,rgb_max=0,alpha_max=0;uint64_t sum=0;
    for(;;){
        int cr=next(&c),mr=next(&m);if(cr==AVERROR_EOF&&mr==AVERROR_EOF)break;assert(cr>=0&&mr>=0);
        int sh=c.v->format==AV_PIX_FMT_YUV420P?1:0;
        assert(c.v->format==AV_PIX_FMT_YUV420P||c.v->format==AV_PIX_FMT_YUV422P);
        assert(av_compare_ts(c.v->best_effort_timestamp,c.f->streams[c.stream]->time_base,m.v->best_effort_timestamp,m.f->streams[m.stream]->time_base)==0);
        s=sws_getCachedContext(s,w,h,c.v->format,w,h,AV_PIX_FMT_RGBA,SWS_BILINEAR,0,0,0);
        g=sws_getCachedContext(g,w,h,m.v->format,w,h,AV_PIX_FMT_GRAY8,SWS_BILINEAR,0,0,0);assert(s&&g);
        uint8_t *dest[]={ref},*adest[]={gray};int stride[]={4*w},astride[]={w};
        assert(sws_scale(s,(const uint8_t*const*)c.v->data,c.v->linesize,0,h,dest,stride)==h);
        assert(sws_scale(g,(const uint8_t*const*)m.v->data,m.v->linesize,0,h,adest,astride)==h);
        for(int y=0;y<h;y++){
            host_video_chroma2_row(c.v->data[1]+(ptrdiff_t)(y>>sh)*c.v->linesize[1],u+y*w,w);
            host_video_chroma2_row(c.v->data[2]+(ptrdiff_t)(y>>sh)*c.v->linesize[2],v+y*w,w);
            host_video_gray_row(m.v->data[0]+(ptrdiff_t)y*m.v->linesize[0],alpha+y*w,w);
            host_video_yuv444_row(c.v->data[0]+(ptrdiff_t)y*c.v->linesize[0],u+y*w,v+y*w,alpha+y*w,out+4*y*w,w);
        }
        for(int i=0;i<n;i++){
            unsigned a=abs(alpha[i]-gray[i]);if(a>alpha_max)alpha_max=a;
            for(int k=0;k<3;k++){unsigned d=abs(out[4*i+k]-ref[4*i+k]);if(d>rgb_max)rgb_max=d;sum+=d;}
        }
        ++frames;
    }
    printf("frames=%u RGB max=%u mean=%.6f/255 alpha max=%u; original PTS matched\n",frames,rgb_max,(double)sum/(frames*(uint64_t)n*3),alpha_max);
    assert(frames&&rgb_max<=3&&alpha_max==0);
    free(p);sws_freeContext(s);sws_freeContext(g);close_reader(&c);close_reader(&m);return 0;
}
