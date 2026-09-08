// Left half: existing image-pattern fill. Right half: production image quads.
// Capture both halves and compare pixels; no game resources or saves are used.
#include <borealis.hpp>
#include <nanovg_gxm.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <cstdio>
#include <vector>

extern "C" { unsigned int _newlib_heap_size_user = 64 * 1024 * 1024; }
static int pattern, atlas;
static unsigned rejected;

class QuadView : public brls::View {
    void draw(NVGcontext *vg, float, float, float, float, brls::Style, brls::FrameContext*) override {
        nvgSave(vg);nvgResetTransform(vg);nvgResetScissor(vg);
        nvgGlobalAlpha(vg,1);nvgGlobalCompositeOperation(vg,NVG_SOURCE_OVER);
        nvgBeginPath(vg);nvgRect(vg,0,0,960,544);
        nvgFillColor(vg,nvgRGBA(20,80,180,255));nvgFill(vg);
        for(int side=0;side<2;side++) for(int cell=0;cell<12;cell++) {
            nvgSave(vg);nvgTranslate(vg,side*480+(cell%3)*160+12,(cell/3)*136+12);
            float u=0,v=0,uw=1,vh=1;
            int image=pattern;
            NVGcolor tint=nvgRGBAf(1,1,1,1);
            if(cell==1) { tint=nvgRGBAf(.35f,.7f,.2f,1);nvgGlobalAlpha(vg,.6f); }
            if(cell==2) { u=1;v=1;uw=-1;vh=-1; }
            if(cell==3) { nvgTranslate(vg,128,0);nvgScale(vg,-1,1); }
            if(cell==4) { nvgTranslate(vg,14,1);nvgRotate(vg,.17f);nvgSkewX(vg,.08f); }
            if(cell==5) { nvgScissor(vg,17,19,71,67); }
            if(cell==6) { nvgGlobalCompositeOperation(vg,NVG_LIGHTER);nvgGlobalAlpha(vg,.25f); }
            if(cell==7) { u=.25f;v=.125f;uw=.5f;vh=.75f;nvgGlobalAlpha(vg,.25f); }
            if(cell==7 || cell==8) {
                image=atlas;u=927.0f/1024;v=787.0f/1024;uw=29.0f/1024;vh=31.0f/1024;
                nvgTranslate(vg,.375f,.3f);
            }
            if(cell==9) { nvgTranslate(vg,0,104);nvgScale(vg,1,-1); }
            if(cell==10) {
                nvgScissor(vg,4,8,107,98);nvgTranslate(vg,4,3);nvgRotate(vg,.04f);
                nvgIntersectScissor(vg,13,11,83,67);
            }
            auto quad=[&](float x,float y,float w,float h,NVGcolor color) {
                if(side) {
                    if(!nvgImageQuad(vg,image,x,y,w,h,u,v,uw,vh,color)) ++rejected;
                } else {
                    NVGpaint paint=nvgImagePattern(vg,x-u*w/uw,y-v*h/vh,w/uw,h/vh,0,image,1);
                    paint.innerColor=paint.outerColor=color;
                    nvgBeginPath(vg);nvgRect(vg,x,y,w,h);nvgFillPaint(vg,paint);nvgFill(vg);
                }
            };
            if(cell==6) for(int j=0;j<4;j++) quad(j*8,j*3,96,84,tint);
            else if(cell==8) {
                const NVGcolor outline=nvgRGBAf(.08f,.05f,.03f,1);
                quad(0,3,112,96,outline);quad(6,3,112,96,outline);
                quad(3,0,112,96,outline);quad(3,6,112,96,outline);
                quad(3,3,112,96,nvgRGBAf(.9f,.85f,.7f,1));
            } else if(cell==11) {
                quad(0,0,112,96,tint);
                nvgBeginPath(vg);nvgRect(vg,18,18,30,30);
                nvgFillColor(vg,nvgRGBA(70,20,30,190));nvgFill(vg);
                quad(17,9,96,82,tint);
            } else quad(0,0,112,96,tint);
            nvgRestore(vg);
        }
        nvgRestore(vg);
    }
};
class QuadActivity : public brls::Activity {
    brls::View *createContentView() override { return new QuadView(); }
};
int main() {
    sceIoMkdir("ux0:/data/art3m1s-image-quad",0777);
    FILE *log=std::fopen("ux0:/data/art3m1s-image-quad/result.log","w");
    if(!log) return 1;
    brls::Logger::setLogOutput(log);
    if(!brls::Application::init()) return 1;
    brls::Application::createWindow("Image quad pixel comparison");
#ifdef ART3M1S_NATIVE_RENDERER
    nvgxmNativeSprites(brls::Application::getNVGContext(),1);
#endif
    brls::Application::setGlobalQuit(false);
    std::vector<unsigned char> pixels(64*64*4);
    for(int y=0;y<64;y++) for(int x=0;x<64;x++) {
        const int i=(y*64+x)*4;
        pixels[i]=x*4;pixels[i+1]=y*4;pixels[i+2]=190;
        pixels[i+3]=x<4||y<4||x>=60||y>=60 ? 0 : ((x/8+y/8)%3)*127;
    }
    pattern=nvgCreateImageRGBA(brls::Application::getNVGContext(),64,64,0,pixels.data());
    if(pattern<=0) return 1;
    pixels.assign(1024*1024*4,0);
    for(int y=0;y<31;y++) for(int x=0;x<29;x++) {
        const int i=((y+787)*1024+x+927)*4;
        pixels[i]=pixels[i+1]=pixels[i+2]=255;
        pixels[i+3]=(x%7==2 || y%9==3) ? 255 : ((x%7==1 || y%9==2) ? 96 : 0);
    }
    atlas=nvgCreateImageRGBA(brls::Application::getNVGContext(),1024,1024,0,pixels.data());
    if(atlas<=0) return 1;
    brls::Application::pushActivity(new QuadActivity());
    unsigned frames=0;
    while(brls::Application::mainLoop()) {
        if(++frames==120) {
            std::fprintf(log,"IMAGE_QUAD_PROBE ready frames=%u rejected=%u; left=legacy right=quad; compare screenshot halves\n",frames,rejected);
            std::fflush(log);
        }
    }
    brls::Logger::setLogOutput(stdout);std::fclose(log);
    sceKernelExitProcess(rejected ? 1 : 0);
    return rejected ? 1 : 0;
}
