// Runs the production direct-video allocator/presenter without sceVideodec.
// This checks GXM NV12 sampling on Vita3K even when hardware decode is absent.
#include "video_direct.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned int _newlib_heap_size_user=64*1024*1024;

static void check(int value,const char *stage){
    if(value<0){sceClibPrintf("[nv12-probe] FAIL %s %d\n",stage,value);sceKernelExitProcess(1);}
}
static void fill(AVFrame *f,int reverse){
    // Limited-range BT.601 white, yellow, cyan, green, magenta, red, blue, black.
    const unsigned char bars[8][3]={{235,128,128},{210,16,146},{170,166,16},{145,54,34},
        {106,202,222},{81,90,240},{41,240,110},{16,128,128}};
    // Magenta padding must never appear below the visible 540-row picture.
    memset(f->data[0],106,960*544);memset(f->data[1],128,960*272);
    for(int y=0;y<540;y++)for(int x=0;x<960;x++){
        int i=reverse?7-x/120:x/120;f->data[0][y*960+x]=bars[i][0];
        if(!(y&1)&&!(x&1)){f->data[1][y/2*960+x]=bars[i][1];f->data[1][y/2*960+x+1]=bars[i][2];}
    }
    for(int y=270;y<272;y++)for(int x=0;x<960;x+=2){f->data[1][y*960+x]=202;f->data[1][y*960+x+1]=222;}
}
int main(void){
    vglInitWithCustomThreshold(1024*1024,960,544,24*1024*1024,32*1024*1024,0,0x8C6000,SCE_GXM_MULTISAMPLE_NONE);
    glViewport(0,0,960,544);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,960,544,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glColor4f(1,1,1,1);glEnable(GL_TEXTURE_2D);
    AVCodecContext ctx={0};host_video_direct_configure(&ctx);
    // Alternate frames, then repeatedly tear down and recreate the whole pool.
    // The final cycle stops changing the bars so screenshots are deterministic.
    for(int cycle=0;cycle<5;cycle++){
        for(int n=0;n<120;n++){
            AVFrame *frame=av_frame_alloc();if(!frame)check(-1,"frame allocation");
            frame->format=AV_PIX_FMT_VITA_NV12;frame->width=960;frame->height=540;
            check(ctx.get_buffer2(&ctx,frame,0),"CDRAM frame");fill(frame,cycle<4&&(n/15)%2);
            check(host_video_direct_present(frame),"present");
            av_frame_free(&frame); // Display must retain its own reference.
            float u,v;host_video_direct_uv(&u,&v);
            glBindTexture(GL_TEXTURE_2D,host_video_direct_texture());glBegin(GL_QUADS);
            glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(u,0);glVertex2f(960,0);
            glTexCoord2f(u,v);glVertex2f(960,544);glTexCoord2f(0,v);glVertex2f(0,544);glEnd();
            vglSwapBuffers(GL_FALSE);
        }
        if(cycle<4){host_video_direct_release_display();host_video_direct_close_pool();}
        sceClibPrintf("[nv12-probe] cycle %d complete\n",cycle+1);
    }
    sceClibPrintf("[nv12-probe] PASS 600 frames, five pool lifetimes; waiting for X\n");
    for(;;){SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CROSS)break;sceKernelDelayThread(16667);}
    host_video_direct_release_display();host_video_direct_close_pool();sceKernelExitProcess(0);return 0;
}
