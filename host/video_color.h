#pragma once
#include <stddef.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

/* Metadata wins. Untagged paired Theora assets sometimes store full-range
 * luma despite using ordinary YUV pixel formats. Infer only from a mask with
 * substantial near-zero AND near-255 regions; ambiguous masks keep TV range.
 * This is an asset compatibility heuristic, not proof of the colour range. */
static int host_video_range_metadata(const AVFrame *f,const AVCodecContext *c){
    int r=f->color_range!=AVCOL_RANGE_UNSPECIFIED?f->color_range:c->color_range;
    if(r==AVCOL_RANGE_JPEG)return 1;
    if(r==AVCOL_RANGE_MPEG)return 0;
    if(f->format==AV_PIX_FMT_YUVJ420P||f->format==AV_PIX_FMT_YUVJ422P||f->format==AV_PIX_FMT_YUVJ444P)return 1;
    return -1;
}
static int host_video_mask_full_range(const AVFrame *f,const AVCodecContext *c){
    int r=host_video_range_metadata(f,c);if(r>=0)return r;
    if(c->codec_id!=AV_CODEC_ID_THEORA||!f->data[0]||
       (f->format!=AV_PIX_FMT_YUV420P&&f->format!=AV_PIX_FMT_YUV422P&&f->format!=AV_PIX_FMT_YUV444P))return 0;
    unsigned low=0,high=0,count=0;
    const int dy=f->height/32>0?f->height/32:1,dx=f->width/32>0?f->width/32:1;
    for(int y=0;y<f->height;y+=dy)for(int x=0;x<f->width;x+=dx){
        unsigned v=f->data[0][(ptrdiff_t)y*f->linesize[0]+x];low+=v<=4;high+=v>=251;++count;
    }
    return count>=64&&low>=count/64+1&&high>=count/64+1;
}
static int host_video_colour_full_range(const AVFrame *f,const AVCodecContext *c,int paired_full_mask){
    int r=host_video_range_metadata(f,c);return r>=0?r:c->codec_id==AV_CODEC_ID_THEORA&&paired_full_mask;
}
static int host_video_colour_matrix(const AVFrame *f,const AVCodecContext *c){
    int s=f->colorspace!=AVCOL_SPC_UNSPECIFIED?f->colorspace:c->colorspace;
    switch(s){
    case AVCOL_SPC_BT709:return SWS_CS_ITU709;
    case AVCOL_SPC_FCC:return SWS_CS_FCC;
    case AVCOL_SPC_SMPTE240M:return SWS_CS_SMPTE240M;
    case AVCOL_SPC_BT2020_NCL:case AVCOL_SPC_BT2020_CL:return SWS_CS_BT2020;
    default:return SWS_CS_ITU601;
    }
}
static int host_video_configure_scaler(struct SwsContext *s,int full,int matrix){
    const int *table=sws_getCoefficients(matrix);
    return sws_setColorspaceDetails(s,table,full,table,1,0,1<<16,1<<16);
}
