#include <borealis.hpp>
#include <nanovg.h>
#include <nanovg_gxm.h>
#include <borealis/platforms/psv/psv_video.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <cstring>

#ifndef ART3M1S_IMAGE_QUAD_AB
#define ART3M1S_IMAGE_QUAD_AB 0
#endif

namespace {
std::unordered_map<uint64_t, int> textures;
bool frame_active = false;
bool font_update_phase = false, font_update_waited = false;
bool font_trace_enabled = false;
uint64_t font_trace_wait_us = 0, font_trace_wait_max_us = 0;
uint64_t font_trace_copy_us = 0, font_trace_bytes = 0, font_trace_updates = 0;
uint32_t uploaded_textures = 0;
uint32_t rejected_uploads = 0;
uint32_t frame_number = 0;
#if ART3M1S_IMAGE_QUAD_AB
bool image_quad_enabled = false;
#endif
uint32_t frame_draws = 0;
uint32_t missing_draws = 0;
uint32_t capture_width = 0, capture_height = 0;
bool capture_requested = false;
std::vector<uint8_t> captured_pixels;
bool game_frame_submitted = false;
const uint8_t* completed_surface = nullptr;
uint32_t completed_width = 0, completed_height = 0, completed_stride = 0;
}

extern "C" void art3m1s_gxm_set_font_trace(int enabled) {
    font_trace_enabled = enabled != 0;
    font_trace_wait_us = font_trace_wait_max_us = font_trace_copy_us = font_trace_bytes = font_trace_updates = 0;
}

extern "C" void art3m1s_gxm_report_font_trace() {
    brls::Logger::info("[nextline-atlas] updates={} bytes={} wait_us={} wait_max_us={} copy_us={}; CPU wall time",
        font_trace_updates, font_trace_bytes, font_trace_wait_us, font_trace_wait_max_us, font_trace_copy_us);
    font_trace_wait_us = font_trace_wait_max_us = font_trace_copy_us = font_trace_bytes = font_trace_updates = 0;
}

extern "C" void art3m1s_gxm_font_update_phase(int enabled) {
    font_update_phase = enabled != 0;
    font_update_waited = false;
}

// Full source image, rectangular destination update. Only called between GPU
// scenes. Wait once per dirty batch; clean frames never enter this path.
extern "C" int art3m1s_gxm_update_texture_region(uint64_t id, uint32_t width, uint32_t height,
    const uint8_t* rgba, size_t length, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!font_update_phase || frame_active || !rgba || !w || !h || x >= width || y >= height ||
        w > width-x || h > height-y || length != size_t(width)*height*4) return 0;
    auto found = textures.find(id);
    auto* vg = brls::Application::getNVGContext();
    if (!vg || found == textures.end()) return 0;
    int tw=0, th=0;
    nvgImageSize(vg, found->second, &tw, &th);
    if (tw != int(width) || th != int(height)) return 0;
    auto* texture = nvgxmImageHandle(vg, found->second);
    if (!texture || !texture->data) return 0;
    if (!font_update_waited) {
        auto* video = static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
        const uint64_t started = font_trace_enabled ? sceKernelGetProcessTimeWide() : 0;
        sceGxmFinish(video->getWindow()->context);
        if (font_trace_enabled) {
            const uint64_t elapsed = sceKernelGetProcessTimeWide()-started;
            font_trace_wait_us += elapsed;
            font_trace_wait_max_us = std::max(font_trace_wait_max_us, elapsed);
        }
        font_update_waited = true;
    }
    const uint64_t copy_started = font_trace_enabled ? sceKernelGetProcessTimeWide() : 0;
    const size_t stride = (size_t(width)+7)&~size_t(7);
    for (uint32_t row=0; row<h; ++row)
        std::memcpy(texture->data+((y+row)*stride+x)*4, rgba+((y+row)*size_t(width)+x)*4, size_t(w)*4);
    if (font_trace_enabled) {
        font_trace_copy_us += sceKernelGetProcessTimeWide()-copy_started;
        font_trace_bytes += uint64_t(w)*h*4;
        ++font_trace_updates;
    }
    return 1;
}

// Called by logic before Borealis begins the next scene. Wait only when a
// screenshot is actually requested, never on the normal frame path.
extern "C" int art3m1s_gxm_read_completed_frame(uint32_t width, uint32_t height, uint8_t* output, size_t length) {
    if (frame_active || !completed_surface || !width || !height || !output || length != size_t(width)*height*4) return 0;
    auto* video = static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    sceGxmFinish(video->getWindow()->context);
    for (uint32_t y=0; y<height; ++y) {
        const size_t sy=uint64_t(y)*completed_height/height;
        if (width == completed_width) {
            std::memcpy(output+size_t(y)*width*4,completed_surface+sy*completed_stride*4,size_t(width)*4);
        } else for (uint32_t x=0; x<width; ++x) {
            const size_t sx=uint64_t(x)*completed_width/width;
            std::memcpy(output+(size_t(y)*width+x)*4,completed_surface+(sy*completed_stride+sx)*4,4);
        }
        // This is the completed display, not a transparent layer. Preserve
        // displayed RGB; never reuse framebuffer alpha as transition coverage.
        for (uint32_t x=0; x<width; ++x) output[(size_t(y)*width+x)*4+3]=255;
    }
    return 1;
}

extern "C" void art3m1s_gxm_reset_readback() {
    completed_surface=nullptr;
    game_frame_submitted=false;
}

// Called only after Borealis has ended the scene and submitted the display
// buffer. Never wait for GXM from inside GameSurface::draw's active scene.
extern "C" void art3m1s_gxm_finish_host_frame() {
    static uint64_t stats_since = 0;
    const uint64_t now = sceKernelGetProcessTimeWide();
    auto* stats_vg = brls::Application::getNVGContext();
    if (stats_vg && (!stats_since || now-stats_since >= 5000000)) {
        NVGXMstats stats{};
        nvgxmTakeStats(stats_vg, &stats);
        if (stats_since && stats.flushes) {
            brls::Logger::info("[gxm-submit-perf] wall_us={} flushes={} nvg_calls={} draw_attempts={} vertices={} texture_lookups={} texture_comparisons={} flush_avg_us={} flush_max_us={}; CPU wall time, excludes endScene/present and GPU completion",
                now-stats_since, stats.flushes, stats.calls, stats.draws, stats.vertices,
                stats.textureLookups, stats.textureComparisons, stats.flushUs/stats.flushes, stats.flushMaxUs);
        }
        stats_since = now;
    }
    if (!game_frame_submitted && !capture_requested) return;
    auto* video = static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    auto* window = video->getWindow();
    auto* fb = window->fb;
    completed_surface = static_cast<const uint8_t*>(fb->gxm_color_surfaces[fb->gxm_front_buffer_index].surface_addr);
    const auto& opts = fb->initOptions;
    completed_width=opts.display_width; completed_height=opts.display_height; completed_stride=opts.display_stride;
    game_frame_submitted=false;
    if (!capture_requested) return;
    captured_pixels.resize(size_t(capture_width) * capture_height * 4);
    art3m1s_gxm_read_completed_frame(capture_width,capture_height,captured_pixels.data(),captured_pixels.size());
    capture_requested = false;
}

extern "C" int art3m1s_gxm_capture_previous(uint32_t width, uint32_t height, uint8_t* output, size_t length) {
    if (!width || !height || !output || length != size_t(width) * height * 4) return 0;
    if (width == capture_width && height == capture_height && captured_pixels.size() == length) {
        std::memcpy(output, captured_pixels.data(), length);
        captured_pixels.clear();
        return 1;
    }
    capture_width = width;
    capture_height = height;
    captured_pixels.clear();
    capture_requested = true;
    return 0;
}

extern "C" void art3m1s_gxm_cancel_capture() {
    capture_requested = false;
    captured_pixels.clear();
}

extern "C" int art3m1s_gxm_upload_texture(
    uint64_t texture, uint32_t width, uint32_t height, const uint8_t* rgba, size_t length) {
    if (!rgba || width == 0 || height == 0 || length != size_t(width) * height * 4) return 0;
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg) return 0;
    auto old = textures.find(texture);
    if (old != textures.end()) {
        nvgDeleteImage(vg, old->second);
        textures.erase(old);
    }
    const int image = nvgCreateImageRGBA(vg, int(width), int(height), 0, rgba);
    if (image <= 0) {
        if (rejected_uploads++ < 8) {
            std::printf("[gxm] upload rejected id=%llu size=%ux%u bytes=%u\n",
                static_cast<unsigned long long>(texture), width, height, unsigned(length));
            std::fflush(stdout);
        }
        return 0;
    }
    textures.emplace(texture, image);
    ++uploaded_textures;
    if (uploaded_textures <= 12) {
        std::printf("[gxm] upload id=%llu image=%d size=%ux%u\n",
            static_cast<unsigned long long>(texture), image, width, height);
        std::fflush(stdout);
    }
    return 1;
}

// Video frames arrive from media_pump before the next scene begins. Reuse
// storage, waiting for prior GPU readers before mutating the existing pixels.
extern "C" int art3m1s_gxm_upload_video_texture(
    uint64_t texture, uint32_t width, uint32_t height, const uint8_t* rgba, size_t length) {
    if (!rgba || !width || !height || length != size_t(width)*height*4 || frame_active) return 0;
    auto found=textures.find(texture);
    auto* vg=brls::Application::getNVGContext();
    if (!vg) return 0;
    if (found != textures.end()) {
        int w=0,h=0;nvgImageSize(vg,found->second,&w,&h);
        if (w==int(width) && h==int(height)) {
            auto* video=static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
            sceGxmFinish(video->getWindow()->context);
            nvgUpdateImage(vg,found->second,rgba);
            return 1;
        }
    }
    return art3m1s_gxm_upload_texture(texture,width,height,rgba,length);
}

extern "C" void art3m1s_gxm_wait_idle() {
    if (frame_active) return;
    auto* video = static_cast<brls::PsvVideoContext*>(brls::Application::getPlatform()->getVideoContext());
    sceGxmFinish(video->getWindow()->context);
}

extern "C" int art3m1s_gxm_release_menu_fonts() {
    if (frame_active) return 0;
    auto* vg = brls::Application::getNVGContext();
    if (!vg) return 0;
    art3m1s_gxm_wait_idle();
    return nvgResetFonts(vg);
}

extern "C" void art3m1s_gxm_delete_texture(uint64_t texture) {
    const auto found = textures.find(texture);
    if (found == textures.end()) return;
    if (NVGcontext* vg = brls::Application::getNVGContext()) nvgDeleteImage(vg, found->second);
    textures.erase(found);
}

extern "C" void art3m1s_gxm_frame_begin(uint32_t stage_width, uint32_t stage_height) {
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg || stage_width == 0 || stage_height == 0) return;
    nvgSave(vg);
    frame_active = true;
    ++frame_number;
#if ART3M1S_IMAGE_QUAD_AB
    image_quad_enabled = (frame_number / 600) % 2 != 0;
    if (frame_number == 1 || frame_number % 600 == 0)
        brls::Logger::info("[gxm-image-quad-ab] frame={} mode={}", frame_number,
            image_quad_enabled ? "quad" : "legacy");
#endif
    frame_draws = 0;
    missing_draws = 0;
    nvgResetTransform(vg);
    nvgResetScissor(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, 960, 544);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
    nvgFill(vg);
    nvgScale(vg, 960.0f / float(stage_width), 544.0f / float(stage_height));
}

extern "C" void art3m1s_gxm_draw_texture(
    uint64_t texture, uint32_t, uint32_t,
    float a, float b, float c, float d, float tx, float ty,
    float quad_width, float quad_height,
    float uv_x, float uv_y, float uv_width, float uv_height,
    float opacity, uint32_t blend,
    float color_r, float color_g, float color_b, int, int,
    uint64_t rule_texture, float rule_progress, float rule_vague,
    int has_clip, float clip_x, float clip_y, float clip_width, float clip_height) {
    if (!frame_active || quad_width <= 0 || quad_height <= 0 || uv_width == 0 || uv_height == 0) return;
    const auto found = textures.find(texture);
    if (found == textures.end()) {
        ++missing_draws;
        return;
    }
    ++frame_draws;
    NVGcontext* vg = brls::Application::getNVGContext();
    nvgSave(vg);
    // Current transform contains stage->display scaling only. The clip already
    // includes ancestor/layer transforms; don't apply the sprite transform twice.
    if (has_clip) nvgScissor(vg, clip_x, clip_y, clip_width, clip_height);
    nvgTransform(vg, a, b, c, d, tx, ty);
    nvgGlobalAlpha(vg, std::clamp(opacity, 0.0f, 1.0f));
    nvgGlobalCompositeOperation(vg, blend == 1 ? NVG_LIGHTER : NVG_SOURCE_OVER);
    bool allow_image_quad = true;
#if ART3M1S_IMAGE_QUAD_AB
    allow_image_quad = image_quad_enabled;
#endif
    if (allow_image_quad && !rule_texture && nvgImageQuad(vg, found->second, 0, 0, quad_width, quad_height,
            uv_x, uv_y, uv_width, uv_height, nvgRGBAf(color_r, color_g, color_b, 1.0f))) {
        static bool reported_quads = false;
        if (!reported_quads) {
            brls::Logger::info("[gxm-image-quad] explicit UV shader active; adjacent identical materials batch in draw order");
            reported_quads = true;
        }
        nvgRestore(vg);
        return;
    }
    const float pattern_width = quad_width / uv_width;
    const float pattern_height = quad_height / uv_height;
    const float pattern_x = -uv_x * pattern_width;
    const float pattern_y = -uv_y * pattern_height;
    NVGpaint paint = nvgImagePattern(
        vg, pattern_x, pattern_y, pattern_width, pattern_height, 0.0f, found->second, 1.0f);
    // Core uses white glyph masks with per-command colors, including black
    // outlines. NanoVG multiplies both paint colors into the sampled image.
    paint.innerColor = paint.outerColor = nvgRGBAf(color_r, color_g, color_b, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, quad_width, quad_height);
    nvgFillPaint(vg, paint);
    bool rule_submitted = false;
    if (rule_texture) {
        const auto rule = textures.find(rule_texture);
        rule_submitted = rule != textures.end() && nvgxmRuleFill(vg, rule->second, rule_progress, rule_vague);
        static bool reported_rule = false, reported_failure = false;
        if (rule_submitted && !reported_rule) {
            brls::Logger::info("[gxm-rule] dual-texture rule draw queued; GPU output requires verification");
            reported_rule = true;
        }
        if (!rule_submitted) {
            if (!reported_failure) {
                brls::Logger::warning("[gxm-rule] shader or rule texture unavailable; using crossfade");
                reported_failure = true;
            }
            nvgGlobalAlpha(vg, std::clamp(opacity, 0.0f, 1.0f) * (1.0f - rule_progress));
        }
    }
    if (!rule_submitted) nvgFill(vg);
    nvgRestore(vg);
}

extern "C" void art3m1s_gxm_frame_end() {
    if (!frame_active) return;
    nvgRestore(brls::Application::getNVGContext());
#if ART3M1S_IMAGE_QUAD_AB
    // Diagnostic captures identify their path without relying on buffered logs.
    // This marker and the automatic alternation do not exist in normal builds.
    auto* vg = brls::Application::getNVGContext();
    nvgSave(vg);nvgResetTransform(vg);nvgResetScissor(vg);
    nvgGlobalAlpha(vg,1);nvgGlobalCompositeOperation(vg,NVG_SOURCE_OVER);
    nvgBeginPath(vg);nvgRect(vg,0,0,6,6);
    nvgFillColor(vg,image_quad_enabled ? nvgRGB(0,255,0) : nvgRGB(255,0,0));
    nvgFill(vg);nvgRestore(vg);
#endif
    frame_active = false;
    game_frame_submitted = true;
    if (frame_number <= 8 || (frame_number % 300) == 0) {
        std::printf("[gxm] frame=%u draws=%u missing=%u textures=%u\n",
            frame_number, frame_draws, missing_draws, unsigned(textures.size()));
        std::fflush(stdout);
    }
}
