#include "video_direct.h"
#include <libavutil/imgutils.h>
#include <psp2/kernel/sysmem.h>
#include <string.h>

// Decoder output, queued frame and displayed frame can coexist. Allocate lazily
// and recycle only after the last AVBuffer reference (including the GPU's) ends.
#define DIRECT_BUFFERS 6
typedef struct {
    SceUID uid;
    uint8_t *data;
    size_t size;
    int busy, pitch, height;
} DirectBuffer;
static DirectBuffer buffers[DIRECT_BUFFERS];
static AVFrame *displayed;
static GLuint display_texture;
static float display_u=1,display_v=1;

static void release_buffer(void *opaque,uint8_t *data){
    (void)data;
    ((DirectBuffer *)opaque)->busy=0;
}

static int get_buffer(AVCodecContext *ctx,AVFrame *pic,int flags){
    (void)flags;
    if(pic->format!=AV_PIX_FMT_VITA_NV12||pic->width<64||pic->height<64||pic->width>1920||pic->height>1088)
        return AVERROR(EINVAL);
    int pitch=(pic->width+15)&~15,height=(pic->height+15)&~15;
    int required=av_image_get_buffer_size(pic->format,pitch,height,1);
    if(required<0)return required;
    size_t size=((size_t)required+0x3ffff)&~(size_t)0x3ffff;
    DirectBuffer *b=NULL;
    for(int i=0;i<DIRECT_BUFFERS;i++)if(!buffers[i].busy&&buffers[i].data&&buffers[i].size>=size){b=&buffers[i];break;}
    if(!b)for(int i=0;i<DIRECT_BUFFERS;i++)if(!buffers[i].data){b=&buffers[i];break;}
    if(!b){av_log(ctx,AV_LOG_ERROR,"[video-direct] frame pool exhausted\n");return AVERROR(ENOMEM);}
    if(!b->data){
        SceUID uid=sceKernelAllocMemBlock("video_nv12",SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,size,NULL);
        if(uid<0){av_log(ctx,AV_LOG_ERROR,"[video-direct] CDRAM allocation failed 0x%x size=%u\n",uid,(unsigned)size);return AVERROR(ENOMEM);}
        void *data=NULL;
        int r=sceKernelGetMemBlockBase(uid,&data);
        if(r>=0)r=sceGxmMapMemory(data,size,SCE_GXM_MEMORY_ATTRIB_READ);
        if(r<0){sceKernelFreeMemBlock(uid);av_log(ctx,AV_LOG_ERROR,"[video-direct] map failed 0x%x\n",r);return AVERROR_EXTERNAL;}
        b->uid=uid;b->data=data;b->size=size;
        av_log(ctx,AV_LOG_INFO,"[video-direct] mapped slot=%d bytes=%u\n",(int)(b-buffers),(unsigned)size);
    }
    int r=av_image_fill_arrays(pic->data,pic->linesize,b->data,pic->format,pitch,height,1);
    if(r<0)return r;
    pic->buf[0]=av_buffer_create(b->data,b->size,release_buffer,b,0);
    if(!pic->buf[0])return AVERROR(ENOMEM);
    b->busy=1;b->pitch=pitch;b->height=height;
    return 0;
}

void host_video_direct_configure(AVCodecContext *ctx){
    ctx->pix_fmt=AV_PIX_FMT_VITA_NV12;
    ctx->get_buffer2=get_buffer;
}

int host_video_direct_present(const AVFrame *frame){
    if(frame->format!=AV_PIX_FMT_VITA_NV12||!frame->buf[0]||frame->crop_left||frame->crop_top||frame->crop_right||frame->crop_bottom)
        return AVERROR(EINVAL);
    DirectBuffer *b=NULL;
    for(int i=0;i<DIRECT_BUFFERS;i++)if(buffers[i].busy&&buffers[i].data==frame->data[0]){b=&buffers[i];break;}
    if(!b||frame->width<1||frame->height<1||frame->width>b->pitch||frame->height>b->height||
       frame->linesize[0]!=b->pitch||frame->linesize[1]!=b->pitch||frame->data[1]!=b->data+(size_t)b->pitch*b->height)
        return AVERROR(EINVAL);
    if(displayed&&displayed->data[0]==frame->data[0])return 0;
    // Linear chroma filtering can sample the aligned padding even with cropped
    // UVs. Extend the visible edge into that small border, never copy the frame.
    for(int y=0;y<frame->height;y++){
        uint8_t *row=b->data+(size_t)y*b->pitch;
        memset(row+frame->width,row[frame->width-1],b->pitch-frame->width);
    }
    for(int y=frame->height;y<b->height;y++)
        memcpy(b->data+(size_t)y*b->pitch,b->data+(size_t)(frame->height-1)*b->pitch,b->pitch);
    int chroma_height=(frame->height+1)/2,chroma_width=(frame->width+1)&~1;
    uint8_t *uv=b->data+(size_t)b->pitch*b->height;
    for(int y=0;y<chroma_height;y++){
        uint8_t *row=uv+(size_t)y*b->pitch;
        for(int x=chroma_width;x<b->pitch;x+=2){row[x]=row[chroma_width-2];row[x+1]=row[chroma_width-1];}
    }
    for(int y=chroma_height;y<b->height/2;y++)
        memcpy(uv+(size_t)y*b->pitch,uv+(size_t)(chroma_height-1)*b->pitch,b->pitch);
    SceGxmTexture native;
    // The UV plane begins after the aligned height, not the visible 540 lines.
    // The quad crops padded rows/columns through normalized UV coordinates.
    int r=sceGxmTextureInitLinear(&native,b->data,SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC0,b->pitch,b->height,0);
    if(r<0){av_log(NULL,AV_LOG_ERROR,"[video-direct] YUV texture failed 0x%x\n",r);return AVERROR_EXTERNAL;}
    sceGxmTextureSetMinFilter(&native,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&native,SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&native,SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&native,SCE_GXM_TEXTURE_ADDR_CLAMP);
    AVFrame *next=av_frame_clone(frame);
    if(!next)return AVERROR(ENOMEM);
    unsigned replacement=host_gxm_video_import(&native);
    if(!replacement){av_frame_free(&next);return AVERROR_EXTERNAL;}
    host_gxm_video_delete(display_texture);
    display_texture=replacement;
    av_frame_free(&displayed);displayed=next;
    display_u=(float)frame->width/b->pitch;display_v=(float)frame->height/b->height;
    return 0;
}

GLuint host_video_direct_texture(void){return display_texture;}
void host_video_direct_uv(float *u,float *v){*u=display_u;*v=display_v;}
void host_video_direct_release_display(void){
    host_gxm_video_delete(display_texture);display_texture=0;
    av_frame_free(&displayed);display_u=display_v=1;
}
void host_video_direct_close_pool(void){
    for(int i=0;i<DIRECT_BUFFERS;i++){
        DirectBuffer *b=&buffers[i];if(!b->data)continue;
        if(b->busy){av_log(NULL,AV_LOG_ERROR,"[video-direct] live frame at pool close slot=%d\n",i);continue;}
        int r=sceGxmUnmapMemory(b->data);
        if(r<0){av_log(NULL,AV_LOG_ERROR,"[video-direct] unmap failed 0x%x\n",r);continue;}
        sceKernelFreeMemBlock(b->uid);memset(b,0,sizeof(*b));
    }
}
