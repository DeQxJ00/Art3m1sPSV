// GPU output test for the production NanoVG GXM rule shader, without game data.
#include <borealis.hpp>
#include <borealis/platforms/psv/psv_video.hpp>
#include <nanovg_gxm.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/stat.h>
#include <psp2/ctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

extern "C" { unsigned int _newlib_heap_size_user = 64 * 1024 * 1024; }
static int old_image, rules[3];
static bool draw_failed;
static const float progress[6] = {0, 1, 0.5f, 0.5f, 0.5f, 0.5f};
static const int rule_index[6] = {0, 1, 0, 1, 2, 2};
static const float softness = 0.25f;
static int screen_w, screen_h;
static const int blend_ops[6] = {NVG_SOURCE_OVER, NVG_LIGHTER, NVG_SOURCE_IN, NVG_SOURCE_OUT, NVG_XOR, NVG_COPY};

class RuleView : public brls::View {
    void draw(NVGcontext *vg, float, float, float, float, brls::Style, brls::FrameContext*) override {
        nvgSave(vg);
        nvgResetTransform(vg);
        nvgResetScissor(vg);
        nvgShapeAntiAlias(vg, 0);
        nvgGlobalAlpha(vg, 1);
        nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, screen_w, screen_h);
        nvgFillColor(vg, nvgRGBA(20, 80, 180, 255));
        nvgFill(vg);
        for (int i=0; i<12; ++i) {
            const float width=screen_w/4, height=screen_h/3;
            const float left=(i%4)*width, top=(i/4)*height;
            nvgGlobalCompositeOperation(vg,i<6 ? NVG_SOURCE_OVER : blend_ops[i-6]);
            // Final cell uses reversed UV, matching captured-image flip semantics.
            NVGpaint paint=nvgImagePattern(vg, left+(i==5 ? width : 0), top,
                i==5 ? -width : width, height, 0, old_image, 1);
            nvgBeginPath(vg); nvgRect(vg,left,top,width,height);
            nvgFillPaint(vg,paint);
            if (i<6) {
                if (!nvgxmRuleFill(vg,rules[rule_index[i]],progress[i],softness)) draw_failed=true;
            } else nvgFill(vg);
        }
        nvgRestore(vg);
    }
};
class RuleActivity : public brls::Activity {
    brls::View *createContentView() override { return new RuleView(); }
};

static float reference_keep(int cell, int local_x, int width) {
    float rule=rule_index[cell]==1 ? 1.0f : 0.0f;
    if (rule_index[cell]==2) {
        float u=(local_x+0.5f)/width;
        if (cell==5) u=1-u;
        // Linear sampling of a 256-texel ramp, texel centers at (x+.5)/256.
        rule=std::clamp((u*256-0.5f)/255,0.0f,1.0f);
    }
    float edge=progress[cell]*(1+softness)-softness;
    float x=std::clamp((rule-edge)/softness,0.0f,1.0f);
    return x*x*(3-2*x);
}

int main() {
    sceIoMkdir("ux0:/data/art3m1s-rule-render",0777);
    FILE *log=std::fopen("ux0:/data/art3m1s-rule-render/result.log","w");
    if (!log) return 1;
    std::fprintf(log,"rule GPU probe %s %s\n",__DATE__,__TIME__); std::fflush(log);
    brls::Logger::setLogOutput(log);
    if (!brls::Application::init()) { std::fprintf(log,"FAIL application init\n"); std::fclose(log); return 1; }
    brls::Application::createWindow("Rule GPU probe");
    brls::Application::setGlobalQuit(false);
    auto *video=static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    auto *window=video->getWindow();
    screen_w=window->fb->initOptions.display_width;
    screen_h=window->fb->initOptions.display_height;
    auto *vg=brls::Application::getNVGContext();
    const unsigned char old_pixel[]={200,40,20,128};
    old_image=nvgCreateImageRGBA(vg,1,1,0,old_pixel);
    std::vector<unsigned char> pixels(256*4);
    for(int r=0;r<3;++r) {
        for(int x=0;x<256;++x) {
            pixels[x*4]=pixels[x*4+1]=pixels[x*4+2]=r==2 ? x : r*255;
            pixels[x*4+3]=255;
        }
        rules[r]=nvgCreateImageRGBA(vg,256,1,0,pixels.data());
    }
    bool ok=old_image>0 && rules[0]>0 && rules[1]>0 && rules[2]>0;
    brls::Application::pushActivity(new RuleActivity());
    // Let the activity's entrance animation settle before checking pixels.
    for(int frame=0;frame<120 && ok;++frame) ok=brls::Application::mainLoop();
    sceGxmFinish(window->context);
    const auto *fb=window->fb;
    const auto *data=static_cast<const unsigned char*>(fb->gxm_color_surfaces[fb->gxm_front_buffer_index].surface_addr);
    const unsigned stride=fb->initOptions.display_stride;
    FILE *ppm=std::fopen("ux0:/data/art3m1s-rule-render/output.ppm","wb");
    if (ppm) {
        std::fprintf(ppm,"P6\n%d %d\n255\n",screen_w,screen_h);
        for(int y=0;y<screen_h;++y) for(int x=0;x<screen_w;++x)
            if(std::fwrite(data+(y*stride+x)*4,1,3,ppm)!=3) ok=false;
        if(std::fclose(ppm)) ok=false;
    } else ok=false;
    const int old_rgb[]={200,40,20}, new_rgb[]={20,80,180};
    for(int cell=0;cell<12;++cell) for(int sample=1;sample<=3;++sample) {
        int width=screen_w/4, height=screen_h/3;
        int local=width*sample/4, x=(cell%4)*width+local, y=(cell/4)*height+height/2;
        const unsigned char *actual=data+(y*stride+x)*4;
        float alpha=(cell<6 ? reference_keep(cell,local,width) : 1.0f)*(128.0f/255);
        float source=alpha, destination=1-alpha, output_alpha=1;
        // Independent premultiplied Porter-Duff reference, destination alpha=1.
        if (cell==7) { destination=1; output_alpha=1; } // LIGHTER, clamped alpha
        if (cell==8 || cell==11) { destination=0; output_alpha=alpha; } // IN / COPY
        if (cell==9) { source=destination=output_alpha=0; } // OUT
        if (cell==10) { source=0; output_alpha=1-alpha; } // XOR
        int expected[3];
        for(int c=0;c<3;++c) {
            expected[c]=std::clamp(int(std::lround(old_rgb[c]*source+new_rgb[c]*destination)),0,255);
            if(std::abs(int(actual[c])-expected[c])>3) ok=false;
        }
        const int expected_alpha=std::lround(output_alpha*255);
        if(std::abs(int(actual[3])-expected_alpha)>1) ok=false;
        std::fprintf(log,"cell=%d sample=%d xy=%d,%d actual=%u,%u,%u,%u expected=%d,%d,%d,%d\n",
            cell,sample,x,y,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected_alpha);
    }
    ok=ok && !draw_failed;
    std::fprintf(log,"%s draw_unavailable=%d; 36 samples, tolerance RGB=3 alpha=1\n",ok ? "PASS" : "FAIL",draw_failed);
    std::fflush(log);
    brls::Logger::setLogOutput(stdout);
    std::fclose(log);
    // Keep rendering until Cross so MCP can inspect the GPU image independently
    // of CPU readback, even on slow instrumented emulator builds.
    while (brls::Application::mainLoop()) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0,&pad,1);
        if (pad.buttons & SCE_CTRL_CROSS) break;
    }
    // Process teardown owns the context and textures; no GPU work remains.
    sceKernelExitProcess(ok ? 0 : 1);
    return 0;
}
