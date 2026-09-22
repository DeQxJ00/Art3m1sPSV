#ifndef HOST_CONVERT_ASSERT
#include <assert.h>
#else
#define assert HOST_CONVERT_ASSERT
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libswscale/swscale.h>
#include "../../host/video_convert.h"

static void borders(void) {
    uint8_t y[96],u[96],v[96],a[96],out[384];
    for(int i=0;i<96;i++){y[i]=i*37;u[i]=i*73;v[i]=i*17;a[i]=i;}
    for(int w=1;w<=65;w++) {
        memset(out,0xcd,sizeof(out));
        host_video_chroma2_row(u+1,out+1,w);
        assert(out[0]==0xcd&&out[w+1]==0xcd);
        for(int x=0;x<w;x++)assert(out[x+1]==u[1+x/2]);
        memset(out,0xcd,sizeof(out));
        host_video_yuv444_row(y+1,u+2,v+3,a+1,out+1,w);
        assert(out[0]==0xcd&&out[1+4*w]==0xcd);
        for(int x=0;x<w;x++) {
            int yy=((int)y[x+1]-16)*76309+32768,uu=(int)u[x+2]-128,vv=(int)v[x+3]-128;
            assert(out[1+4*x]==host_video_clip((yy+104597*vv)>>16));
            assert(out[2+4*x]==host_video_clip((yy-25675*uu-53279*vv)>>16));
            assert(out[3+4*x]==host_video_clip((yy+132201*uu)>>16));
            assert(out[4+4*x]==a[x+1]);
        }
        host_video_yuv444_row(y,u,v,NULL,out,w);
        for(int x=0;x<w;x++)assert(out[4*x+3]==255);
        uint8_t q6[384];memset(q6,0xcd,sizeof(q6));
        host_video_yuv444_q6_row(y+1,u+2,v+3,a+1,q6+1,w);
        host_video_yuv444_row(y+1,u+2,v+3,a+1,out+1,w);
        assert(q6[0]==0xcd&&q6[1+4*w]==0xcd);
        for(int x=0;x<w;x++)for(int c=0;c<4;c++)
            assert(abs((int)q6[1+4*x+c]-out[1+4*x+c])<=(c==3?0:1));
        host_video_yuv444_q6_row(y,u,v,NULL,q6,w);
        for(int x=0;x<w;x++)assert(q6[4*x+3]==255);
    }
}
static void subsampled(void){
    enum {W=64,H=32,N=W*H};
    uint8_t *p=calloc(1,16*N+256);assert(p);
    uint8_t *y=p,*u=p+N,*v=p+2*N,*ref=p+3*N,*out=p+7*N,*up=p+11*N,*vp=p+12*N;
    for(int sh=0;sh<=1;sh++){
        enum AVPixelFormat f=sh?AV_PIX_FMT_YUV420P:AV_PIX_FMT_YUV422P;
        for(int i=0;i<N;i++){y[i]=16+(i*37)%220;u[i]=16+(i*53)%225;v[i]=16+(i*71)%225;}
        const uint8_t *src[]={y,u,v};int stride[]={W,W/2,W/2},ds[]={4*W};uint8_t *dst[]={ref};
        struct SwsContext *s=sws_getContext(W,H,f,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,0,0,0);assert(s);
        assert(sws_scale(s,src,stride,0,H,dst,ds)==H);sws_freeContext(s);
        for(int r=0;r<H;r++){
            host_video_chroma2_row(u+(r>>sh)*W/2,up+r*W,W);
            host_video_chroma2_row(v+(r>>sh)*W/2,vp+r*W,W);
            host_video_yuv444_row(y+r*W,up+r*W,vp+r*W,NULL,out+r*W*4,W);
        }
        int max=0;for(int i=0;i<4*N;i++){int d=abs(out[i]-ref[i]);if(d>max)max=d;assert(d<=3);}
        s=sws_getContext(W,H,f,W,H,AV_PIX_FMT_GRAY8,SWS_BILINEAR,0,0,0);assert(s);ds[0]=W;
        for(int i=0;i<N;i++)y[i]=(uint8_t)i;
        assert(sws_scale(s,src,stride,0,H,dst,ds)==H);sws_freeContext(s);
        for(int r=0;r<H;r++)host_video_gray_row(y+r*W,out+r*W,W);
        assert(!memcmp(out,ref,N));
        printf("subsample format=%d RGB max=%d/255, all alpha levels exact\n",f,max);
    }
    free(p);
}
static void levels(void) {
    enum {W=256,H=256};
    uint8_t *p=calloc(1,W*H*12+256);assert(p);
    uint8_t *y=p,*u=y+W*H,*v=u+W*H,*out=v+W*H,*ref=out+W*H*4;
    const uint8_t *src[]={y,u,v};int stride[]={W,W,W},os[]={W*4};uint8_t *dst[]={ref};
    struct SwsContext *s=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,0,0,0);assert(s);
    int max=0,q6max=0;
    // Full legal Y/U/V range, repeated chroma within each row. Avoid swscale's
    // architecture-specific overflow for extreme out-of-range synthetic YUV.
    for(int vv=16;vv<=240;vv++) {
        for(int r=0;r<H;r++)for(int x=0;x<W;x++){y[r*W+x]=16+x*219/255;u[r*W+x]=16+r*224/255;v[r*W+x]=vv;}
        assert(sws_scale(s,src,stride,0,H,dst,os)==H);
        for(int r=0;r<H;r++)host_video_yuv444_row(y+r*W,u+r*W,v+r*W,0,out+r*W*4,W);
        for(int i=0;i<W*H*4;i++){int d=abs(ref[i]-out[i]);if(d>max)max=d;assert(d<=1);}
        for(int r=0;r<H;r++)host_video_yuv444_q6_row(y+r*W,u+r*W,v+r*W,0,out+r*W*4,W);
        for(int i=0;i<W*H*4;i++){int d=abs(ref[i]-out[i]);if(d>q6max)q6max=d;assert(d<=1);}
    }
    sws_freeContext(s);
    s=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_GRAY8,SWS_BILINEAR,0,0,0);assert(s);os[0]=W;
    for(int r=0;r<H;r++)for(int x=0;x<W;x++)y[r*W+x]=x;
    assert(sws_scale(s,src,stride,0,H,dst,os)==H);
    for(int r=0;r<H;r++)host_video_gray_row(y+r*W,out+r*W,W);
    assert(!memcmp(ref,out,W*H));sws_freeContext(s);free(p);
    printf("legal YUV cube RGB wide_max=%d Q6_max=%d/255; all 256 alpha levels exact\n",max,q6max);
}
static void q6_cube(void) {
    enum {W=256,H=256,N=W*H};
    uint8_t *p=calloc(1,11*N);assert(p);
    uint8_t *y=p,*u=y+N,*v=u+N,*wide=v+N,*q6=wide+4*N;
    for(int r=0;r<H;r++)for(int x=0;x<W;x++){y[r*W+x]=x;u[r*W+x]=r;}
    int max=0;
    for(int vv=0;vv<256;vv++){
        memset(v,vv,N);
        for(int r=0;r<H;r++){
            host_video_yuv444_row(y+r*W,u+r*W,v+r*W,NULL,wide+4*r*W,W);
            host_video_yuv444_q6_row(y+r*W,u+r*W,v+r*W,NULL,q6+4*r*W,W);
        }
        for(int i=0;i<4*N;i++){int d=abs((int)wide[i]-q6[i]);if(d>max)max=d;assert(d<=1);}
    }
    printf("Q6 complete 256^3 YUV cube: max_error=%d/255\n",max);free(p);
}
static void recording(const char *color,const char *mask) {
    const int w=960,h=540,n=w*h;FILE *fc=fopen(color,"rb"),*fm=fopen(mask,"rb");assert(fc&&fm);
    uint8_t *p=calloc(1,15*n+256);assert(p);
    uint8_t *y=p,*m=y+3*n,*ref=m+3*n,*out=ref+4*n,*alpha=out+4*n;
    struct SwsContext *s=sws_getContext(w,h,AV_PIX_FMT_YUV444P,w,h,AV_PIX_FMT_RGBA,SWS_BILINEAR,0,0,0);
    struct SwsContext *g=sws_getContext(w,h,AV_PIX_FMT_YUV444P,w,h,AV_PIX_FMT_GRAY8,SWS_BILINEAR,0,0,0);
    assert(s&&g);int frames=0,max=0,q6max=0;unsigned long long sum=0,count=0;
    while(fread(y,1,3*n,fc)==(size_t)(3*n)) {
        assert(fread(m,1,3*n,fm)==(size_t)(3*n));
        const uint8_t *src[]={y,y+n,y+2*n},*ms[]={m,m+n,m+2*n};int stride[]={w,w,w},rs[]={w*4},gs[]={w};uint8_t *dest[]={ref},*gray[]={alpha};
        sws_scale(s,src,stride,0,h,dest,rs);sws_scale(g,ms,stride,0,h,gray,gs);
        for(int r=0;r<h;r++) {
            // Exercise row stride handling used by video.c; full 4:4:4 colour.
            host_video_gray_row(m+r*w,out+r*w,w);
            assert(!memcmp(out+r*w,alpha+r*w,w));
        }
        for(int r=0;r<h;r++)host_video_yuv444_row(y+r*w,y+n+r*w,y+2*n+r*w,alpha+r*w,out+4*r*w,w);
        for(int i=0;i<n;i++) {
            assert(out[4*i+3]==alpha[i]);
            for(int c=0;c<3;c++){int d=abs(out[4*i+c]-ref[4*i+c]);if(d>max)max=d;sum+=d;count++;}
        }
        for(int r=0;r<h;r++)host_video_yuv444_q6_row(y+r*w,y+n+r*w,y+2*n+r*w,alpha+r*w,out+4*r*w,w);
        for(int i=0;i<n;i++) {
            assert(out[4*i+3]==alpha[i]);
            for(int c=0;c<3;c++){int d=abs(out[4*i+c]-ref[4*i+c]);if(d>q6max)q6max=d;}
        }
        frames++;
    }
    printf("recording frames=%d RGB max_error=%d mean_error=%.6f Q6_max=%d alpha exact\n",frames,max,(double)sum/count,q6max);
    assert(frames>0&&max<=2&&q6max<=1);sws_freeContext(s);sws_freeContext(g);free(p);fclose(fc);fclose(fm);
}
int main(int argc,char **argv){borders();levels();q6_cube();subsampled();if(argc==3)recording(argv[1],argv[2]);return 0;}
