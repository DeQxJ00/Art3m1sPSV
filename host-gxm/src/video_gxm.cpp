#include "video_gxm.h"
#include <borealis.hpp>
#include <borealis/platforms/psv/psv_video.hpp>
#include <nanovg_gxm.h>

extern "C" void host_gxm_video_wait() {
    auto* video = static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    sceGxmFinish(video->getWindow()->context);
}
extern "C" unsigned host_gxm_video_rgba(unsigned image, int width, int height, const uint8_t* pixels) {
    auto* vg = brls::Application::getNVGContext();
    host_gxm_video_wait();
    if (image) {
        int old_width, old_height;
        nvgImageSize(vg, image, &old_width, &old_height);
        if (old_width == width && old_height == height) {
            nvgUpdateImage(vg, image, pixels);
            return image;
        }
        nvgDeleteImage(vg, image);
    }
    return nvgCreateImageRGBA(vg, width, height, 0, pixels);
}
extern "C" unsigned host_gxm_video_import(SceGxmTexture* texture) {
    return nvgxmCreateImageFromHandle(brls::Application::getNVGContext(), texture);
}
extern "C" void host_gxm_video_delete(unsigned image) {
    if (!image) return;
    host_gxm_video_wait();
    nvgDeleteImage(brls::Application::getNVGContext(), image);
}
extern "C" void host_gxm_video_draw(unsigned image, float u, float v) {
    if (!image || u <= 0 || v <= 0) return;
    auto* vg = brls::Application::getNVGContext();
    nvgSave(vg);
    nvgResetTransform(vg);
    nvgResetScissor(vg);
    nvgGlobalAlpha(vg, 1);
    nvgGlobalCompositeOperation(vg, NVG_SOURCE_OVER);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, 960, 544);
    nvgFillPaint(vg, nvgImagePattern(vg, 0, 0, 960 / u, 544 / v, 0, image, 1));
    nvgFill(vg);
    nvgRestore(vg);
}
