#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../host/video_color.h"
#include "../../host/video_convert.h"

int main(void){
    enum {W=64,H=32,N=W*H};
    uint8_t y[N],u[N],v[N],mask[(W+8)*H],ref[N*4],out[N*4];
    AVCodecContext c={0};c.codec_id=AV_CODEC_ID_THEORA;
    c.color_range=AVCOL_RANGE_UNSPECIFIED;c.colorspace=AVCOL_SPC_UNSPECIFIED;
    AVFrame f={0};f.width=W;f.height=H;f.format=AV_PIX_FMT_YUV420P;
    f.color_range=AVCOL_RANGE_UNSPECIFIED;f.colorspace=AVCOL_SPC_UNSPECIFIED;
    f.data[0]=mask;f.linesize[0]=W+8;
    for(int r=0;r<H;r++)for(int x=0;x<W+8;x++)mask[r*(W+8)+x]=x<W/2?0:255;
    assert(host_video_mask_full_range(&f,&c));
    assert(host_video_colour_full_range(&f,&c,1));
    assert(!host_video_colour_full_range(&f,&c,0));
    f.color_range=AVCOL_RANGE_MPEG;
    assert(!host_video_mask_full_range(&f,&c)&&!host_video_colour_full_range(&f,&c,1));
    f.color_range=AVCOL_RANGE_JPEG;assert(host_video_colour_full_range(&f,&c,0));
    f.color_range=AVCOL_RANGE_UNSPECIFIED;c.color_range=AVCOL_RANGE_MPEG;
    assert(!host_video_colour_full_range(&f,&c,1));c.color_range=AVCOL_RANGE_UNSPECIFIED;
    for(int r=0;r<H;r++)for(int x=0;x<W+8;x++)mask[r*(W+8)+x]=x<W/2?16:235;
    assert(!host_video_mask_full_range(&f,&c));
    memset(mask,128,sizeof(mask));assert(!host_video_mask_full_range(&f,&c));
    memset(mask,255,sizeof(mask));assert(!host_video_mask_full_range(&f,&c));
    c.codec_id=AV_CODEC_ID_H264;assert(!host_video_colour_full_range(&f,&c,1));
    f.colorspace=AVCOL_SPC_BT709;assert(host_video_colour_matrix(&f,&c)==SWS_CS_ITU709);
    f.colorspace=AVCOL_SPC_UNSPECIFIED;assert(host_video_colour_matrix(&f,&c)==SWS_CS_ITU601);
    unsigned maximum=0;
    for(unsigned pass=0;pass<256;pass++){
        for(unsigned i=0;i<N;i++){y[i]=i;u[i]=(i/256)*32+pass%32;v[i]=pass;}
        const uint8_t* src[]={y,u,v};int stride[]={W,W,W},dstStride[]={W*4};uint8_t* dst[]={ref};
        struct SwsContext *s=sws_getContext(W,H,AV_PIX_FMT_YUV444P,W,H,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);assert(s);
        assert(host_video_configure_scaler(s,1,SWS_CS_ITU601)>=0);
        sws_scale(s,src,stride,0,H,dst,dstStride);sws_freeContext(s);
        for(int r=0;r<H;r++)host_video_yuv444_full_row(y+r*W,u+r*W,v+r*W,NULL,out+r*W*4,W);
        for(unsigned i=0;i<N*4;i++){unsigned d=abs((int)ref[i]-out[i]);if(d>maximum)maximum=d;}
    }
    assert(maximum<=2);
    for(unsigned i=0;i<256;i++)y[i]=i;
    host_video_gray_range_row(y,out,256,1);assert(!memcmp(y,out,256));
    for(int width=1;width<=63;width++){
        memset(out,0xcc,sizeof(out));host_video_yuv444_full_row(y,u,v,NULL,out+1,width);
        assert(out[0]==0xcc&&out[1+width*4]==0xcc);
    }
    printf("range policy: metadata, paired endpoints, ambiguous masks, independent alpha passed; full RGB max=%u/255\n",maximum);
    return 0;
}
