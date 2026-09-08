#include <borealis.hpp>
extern "C" {
#include "video_direct.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/ctrl.h>
}
#include <cstring>

extern "C" unsigned int _newlib_heap_size_user = 64 * 1024 * 1024;
static void check(int result, const char* stage) {
    if (result < 0) {
        sceClibPrintf("[gxm-nv12-probe] FAIL %s: %d\n", stage, result);
        sceKernelExitProcess(1);
    }
}
static void fill(AVFrame* frame, bool reverse) {
    const uint8_t bars[8][3] = {{235,128,128},{210,16,146},{170,166,16},{145,54,34},
        {106,202,222},{81,90,240},{41,240,110},{16,128,128}};
    std::memset(frame->data[0], 106, 960*544);
    std::memset(frame->data[1], 128, 960*272);
    for (int y=0; y<540; ++y) for (int x=0; x<960; ++x) {
        const int index = reverse ? 7-x/120 : x/120;
        frame->data[0][y*960+x] = bars[index][0];
        if (!(y&1) && !(x&1)) {
            frame->data[1][y/2*960+x] = bars[index][1];
            frame->data[1][y/2*960+x+1] = bars[index][2];
        }
    }
    for(int y=270;y<272;++y) for(int x=0;x<960;x+=2) {
        frame->data[1][y*960+x]=202; frame->data[1][y*960+x+1]=222;
    }
}
class ProbeView : public brls::View {
    void draw(NVGcontext*, float, float, float, float, brls::Style, brls::FrameContext*) override {
        float u,v; host_video_direct_uv(&u,&v);
        host_gxm_video_draw(host_video_direct_texture(),u,v);
    }
};
class ProbeActivity : public brls::Activity {
    brls::View* createContentView() override { return new ProbeView(); }
};
int main() {
    if (!brls::Application::init()) return 1;
    brls::Application::createWindow("GXM NV12 probe");
    brls::Application::setGlobalQuit(false);
    brls::Application::pushActivity(new ProbeActivity());
    AVCodecContext decoder{};
    host_video_direct_configure(&decoder);
    unsigned count=0;
    for (;;) {
        if (count<600) {
            if (count && count%120==0) {
                host_video_direct_release_display(); host_video_direct_close_pool();
            }
            AVFrame* frame=av_frame_alloc(); if(!frame) check(-1,"frame allocation");
            frame->format=AV_PIX_FMT_VITA_NV12; frame->width=960; frame->height=540;
            check(decoder.get_buffer2(&decoder,frame,0),"CDRAM allocation");
            fill(frame,count<480 && (count/15)%2);
            check(host_video_direct_present(frame),"native GXM import");
            av_frame_free(&frame);
        }
        if (!brls::Application::mainLoop()) return 1;
        ++count;
        if (count<=600 && count%120==0) sceClibPrintf("[gxm-nv12-probe] cycle %u complete\n",count/120);
        if (count==600) sceClibPrintf("[gxm-nv12-probe] PASS 600 frames, five pool lifetimes; X exits\n");
        if (count>=600) {
            SceCtrlData pad{}; sceCtrlPeekBufferPositive(0,&pad,1);
            if (pad.buttons&SCE_CTRL_CROSS) {
                host_video_direct_release_display(); host_video_direct_close_pool();
                sceClibPrintf("[gxm-nv12-probe] cleanup complete\n");
                sceKernelExitProcess(0);
            }
        }
    }
}
