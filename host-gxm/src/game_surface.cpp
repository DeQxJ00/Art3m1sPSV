#include "game_surface.hpp"

#include "files.h"
#include "runtime_api.h"
#include "media_gxm.hpp"
extern "C" {
#include "video.h"
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>

namespace {

std::atomic<int> loading_archive_done {0}, loading_archive_total {0};

void core_log(const char* level, const char* message) {
    brls::Logger::info("[core:{}] {}", level ? level : "?", message ? message : "");
    std::printf("[core:%s] %s\n", level ? level : "?", message ? message : "");
    std::fflush(stdout);
}

uint8_t* read_file(const char* path, size_t* length) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size <= 0) { std::fclose(file); return nullptr; }
    auto* bytes = static_cast<uint8_t*>(std::malloc(size_t(size)));
    if (!bytes || std::fread(bytes, 1, size_t(size), file) != size_t(size)) {
        std::free(bytes);
        std::fclose(file);
        return nullptr;
    }
    std::fclose(file);
    *length = size_t(size);
    return bytes;
}

} // namespace

extern "C" void host_loading_show(int stage, const char* detail) {
    int current = 0, total = 0;
    if (stage == 2 && detail && std::sscanf(detail, "PFS %d / %d", &current, &total) == 2) {
        loading_archive_total.store(total);
        loading_archive_done.store(std::max(0, current - 1));
    }
}
extern "C" void host_loading_finish() {}
extern "C" void art3m1s_gxm_cancel_capture();
extern "C" void art3m1s_gxm_reset_readback();
extern "C" int art3m1s_gxm_release_menu_fonts();
extern "C" void art3m1s_gxm_wait_idle();
extern "C" int art3m1s_runtime_prepare_gxm_textures(void*);
extern "C" void art3m1s_gxm_font_update_phase(int);
extern "C" void art3m1s_gxm_set_font_trace(int);
extern "C" void art3m1s_gxm_report_font_trace();
extern "C" void art3m1s_runtime_set_profiler_enabled(const void*, int);
extern "C" int art3m1s_runtime_profiler_snapshot(const void*, uint8_t*, uint32_t);

namespace art3m1s {

namespace { GameSurface* active_game = nullptr; }

void gxm_game_tick() {
    if (active_game) active_game->tick();
}

GameSurface::GameSurface(GameEntry game) : game_(std::move(game)) {
    setFocusable(true);
    setHideHighlight(true);
    loading_archive_done.store(0);
    loading_archive_total.store(0);
    active_game = this;
}

void* GameSurface::load_archives(void* surface) {
    auto* self = static_cast<GameSurface*>(surface);
    const std::string save = std::string(kDataRoot) + "/saves/" + self->game_.id;
    sceIoMkdir((std::string(kDataRoot) + "/saves").c_str(), 0777);
    const int result = host_files_open(self->game_.path.c_str(), save.c_str());
    loading_archive_done.store(loading_archive_total.load());
    self->archive_result_.store(result);
    return nullptr;
}

void GameSurface::initialize() {
    art3m1s_gxm_reset_readback();
    std::printf("[host] initialize game=%s path=%s\n", game_.id.c_str(), game_.path.c_str());
    std::fflush(stdout);
    art3m1s_register_log_callback(core_log);
    art3m1s_register_file_reader(host_read);
    art3m1s_register_file_writer(host_write);
    art3m1s_register_file_delete(host_delete);
    runtime_ = art3m1s_runtime_create(960, 544, 5);
    if (!runtime_) {
        error_ = "无法创建 GXM 运行时";
        return;
    }
    gxm_media_attach(runtime_);
    art3m1s_register_media_command_callback(gxm_media_command);
    size_t ini_length = 0;
    uint8_t* ini = read_file((game_.path + "/system.ini").c_str(), &ini_length);
    if (!ini) {
        const int size = host_read("system.ini", nullptr, 0, -1);
        if (size > 0) {
            ini = static_cast<uint8_t*>(std::malloc(size_t(size)));
            if (ini && host_read("system.ini", ini, size, 0) == size) ini_length = size_t(size);
        }
    }
    if (!ini || art3m1s_runtime_load_project_bytes(runtime_, ini, ini_length, "WINDOWS") != 0) {
        std::free(ini);
        error_ = "加载 system.ini 或启动脚本失败";
        return;
    }
    std::free(ini);
    SceIoStat trace_stat{};
    trace_nextline_ = sceIoGetstat("ux0:data/art3m1s-gxm/trace-nextline.flag", &trace_stat) >= 0;
    art3m1s_gxm_set_font_trace(trace_nextline_ ? 1 : 0);
    if (trace_nextline_) {
        art3m1s_runtime_set_profiler_enabled(runtime_, 1);
        brls::Logger::info("[nextline-trace] enabled; diagnostic profiling adds overhead; 5-second reports are not frame samples");
    }
    previous_time_ = sceKernelGetProcessTimeWide();
    trace_since_ = previous_time_;
    loaded_ = true;
    std::printf("[host] runtime loaded stage=%ux%u\n",
        art3m1s_runtime_stage_width(runtime_), art3m1s_runtime_stage_height(runtime_));
    std::fflush(stdout);
}

GameSurface::~GameSurface() {
    if (active_game == this) active_game = nullptr;
    if (loading_thread_started_) pthread_join(loading_thread_, nullptr);
    art3m1s_gxm_cancel_capture();
    art3m1s_gxm_reset_readback();
    gxm_media_detach();
    if (runtime_) art3m1s_runtime_destroy(runtime_);
    if (menu_fonts_released_) brls::Application::getPlatform()->getFontLoader()->loadFonts();
}

void GameSurface::feed_input() {
    SceCtrlData pad {};
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const uint32_t changed = previous_buttons_ ^ pad.buttons;
    if (changed & pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) gxm_media_skip();
    struct Mapping { uint32_t button; uint32_t key; };
    static const Mapping mappings[] = {
        { SCE_CTRL_CIRCLE, 13 }, { SCE_CTRL_CROSS, 27 }, { SCE_CTRL_START, 13 },
        { SCE_CTRL_RTRIGGER, 17 }, { SCE_CTRL_UP, 38 }, { SCE_CTRL_DOWN, 40 },
    };
    for (const Mapping& mapping : mappings) {
        if (changed & mapping.button) art3m1s_runtime_feed_key(runtime_, mapping.key, (pad.buttons & mapping.button) != 0);
    }
    if (std::abs(int(pad.lx) - 128) > 24) mouse_x_ += (int(pad.lx) - 128) / 20;
    if (std::abs(int(pad.ly) - 128) > 24) mouse_y_ += (int(pad.ly) - 128) / 20;
    const int stage_width = int(art3m1s_runtime_stage_width(runtime_));
    const int stage_height = int(art3m1s_runtime_stage_height(runtime_));
    mouse_x_ = std::clamp(mouse_x_, 0, std::max(stage_width - 1, 0));
    mouse_y_ = std::clamp(mouse_y_, 0, std::max(stage_height - 1, 0));
    art3m1s_runtime_feed_mouse(runtime_, mouse_x_, mouse_y_);

    SceTouchData touch {};
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
    if (touch.reportNum && !previous_touch_.reportNum) gxm_media_skip();
    if (touch.reportNum) {
        mouse_x_ = int(touch.report[0].x) * stage_width / 1920;
        mouse_y_ = int(touch.report[0].y) * stage_height / 1088;
        art3m1s_runtime_feed_mouse(runtime_, mouse_x_, mouse_y_);
    }
    art3m1s_runtime_feed_mouse_button(runtime_, 1, touch.reportNum > 0 || (pad.buttons & SCE_CTRL_SQUARE));
    previous_touch_ = touch;
    previous_buttons_ = pad.buttons;

}

void GameSurface::tick() {
    if (leaving_) {
        // Free game/media memory before allocating the menu fonts again.
        art3m1s_gxm_cancel_capture();
        art3m1s_gxm_reset_readback();
        gxm_media_detach();
        gxm_media_pump();
        art3m1s_gxm_wait_idle();
        if (runtime_) { art3m1s_runtime_destroy(runtime_); runtime_ = nullptr; }
        // Restore the same packaged font registration order before the menu
        // resumes/layouts. Never mutate atlas textures inside a GXM scene.
        if (menu_fonts_released_) {
            brls::Application::getPlatform()->getFontLoader()->loadFonts();
            menu_fonts_released_ = false;
            brls::Logger::info("[menu-memory] fonts restored");
        }
        brls::Application::popActivity(brls::TransitionAnimation::NONE);
        return;
    }
    if (!loaded_ && error_.empty()) {
        if (loading_stage_ == 0) {
            loading_stage_ = 1;
            if (pthread_create(&loading_thread_, nullptr, load_archives, this) != 0) {
                error_ = "无法启动资源读取线程";
            } else loading_thread_started_ = true;
        } else if (loading_stage_ == 1 && archive_result_.load() != -999) {
            pthread_join(loading_thread_, nullptr);
            loading_thread_started_ = false;
            if (archive_result_.load() < 0) error_ = "无法打开游戏目录";
            else loading_stage_ = 2; // Show the next stage before synchronous core boot.
        } else if (loading_stage_ == 2) {
            loading_stage_ = 3;
            initialize(); // Core/GXM remain exclusively on the main thread.
        }
        return;
    }
    if (!loaded_ || leaving_) return;
    if (!menu_fonts_released_) {
        menu_fonts_released_ = art3m1s_gxm_release_menu_fonts() != 0;
        if (menu_fonts_released_) brls::Logger::info("[menu-memory] font data and glyph atlas released");
    }
    const uint64_t now = sceKernelGetProcessTimeWide();
    uint32_t delta = uint32_t((now - previous_time_) / 1000);
    previous_time_ = now;
    delta = std::clamp(delta, 1u, 100u);
    // Input collected during the preceding draw is consumed here, outside any
    // GXM scene, so takess can wait once without stalling every displayed frame.
    art3m1s_runtime_advance_without_render(runtime_, delta);
    const uint64_t logic_done = trace_nextline_ ? sceKernelGetProcessTimeWide() : 0;
    art3m1s_gxm_font_update_phase(1);
    art3m1s_runtime_prepare_gxm_textures(runtime_);
    art3m1s_gxm_font_update_phase(0);
    if (trace_nextline_) {
        const uint64_t prepared = sceKernelGetProcessTimeWide();
        trace_logic_max_ = std::max(trace_logic_max_, logic_done - now);
        trace_prepare_max_ = std::max(trace_prepare_max_, prepared - logic_done);
        if (prepared - now > 16667) ++trace_slow_count_;
        if (prepared - trace_since_ >= 5000000) {
            brls::Logger::info("[nextline-trace] logic_max_us={} atlas_prepare_max_us={} tick_over16ms={}; maxima may be different frames",
                trace_logic_max_, trace_prepare_max_, trace_slow_count_);
            art3m1s_gxm_report_font_trace();
            // Use the existing asynchronous aggregator, not per-event logging.
            // A fixed buffer avoids repeated allocations on the host side.
            static uint8_t snapshot[32768];
            const int bytes = art3m1s_runtime_profiler_snapshot(runtime_, snapshot, sizeof(snapshot)-1);
            if (bytes > 0 && bytes < int(sizeof(snapshot))) {
                snapshot[bytes] = 0;
                brls::Logger::info("[nextline-core] {}", reinterpret_cast<const char*>(snapshot));
            } else brls::Logger::info("[nextline-core] snapshot unavailable bytes={}", bytes);
            const uint64_t reported = sceKernelGetProcessTimeWide();
            brls::Logger::info("[nextline-trace] report_cost_us={}; exclude reporting from gameplay cost", reported-prepared);
            trace_since_ = reported;
            trace_logic_max_ = trace_prepare_max_ = trace_slow_count_ = 0;
        }
    }
}

void GameSurface::draw(NVGcontext* vg, float, float, float, float, brls::Style, brls::FrameContext*) {
    if (!loaded_) {
        nvgSave(vg);
        nvgResetTransform(vg);
        nvgResetScissor(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, 960, 544);
        nvgFillColor(vg, nvgRGB(12, 18, 28));
        nvgFill(vg);
        nvgFontFaceId(vg, brls::Application::getDefaultFont());
        nvgFontSize(vg, 25);
        nvgFillColor(vg, nvgRGB(240, 244, 250));
        nvgText(vg, 48, 100, error_.empty() ? "正在加载游戏" : error_.c_str(), nullptr);
        if (error_.empty()) {
            const int done = loading_archive_done.load(), total = loading_archive_total.load();
            const float progress = loading_stage_ >= 2 ? 0.8f :
                (total > 0 ? 0.1f + 0.6f * float(done) / float(total) : 0.05f);
            const std::string detail = loading_stage_ >= 2 ? "正在初始化引擎与启动脚本" :
                (total > 0 ? "读取资源包 " + std::to_string(done) + " / " + std::to_string(total) : "正在扫描游戏资源");
            nvgFontSize(vg, 21);
            nvgText(vg, 48, 155, game_.title.c_str(), nullptr);
            nvgText(vg, 48, 215, detail.c_str(), nullptr);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, 48, 250, 864, 16, 8);
            nvgFillColor(vg, nvgRGB(42, 52, 68));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, 48, 250, 864 * progress, 16, 8);
            nvgFillColor(vg, nvgRGB(80, 195, 235));
            nvgFill(vg);
        }
        nvgRestore(vg);
        return;
    }
    feed_input();
    if (leaving_) return;
    const uint64_t draw_start=sceKernelGetProcessTimeWide();
    art3m1s_runtime_present_gxm(runtime_);
    static uint64_t draw_report=0, draw_total=0, draw_max=0;
    static unsigned draw_samples=0;
    const uint64_t draw_end=sceKernelGetProcessTimeWide();
    draw_total+=draw_end-draw_start;draw_max=std::max(draw_max,draw_end-draw_start);++draw_samples;
    if (!draw_report) draw_report=draw_end;
    if(draw_end-draw_report>=5000000) {
        brls::Logger::info("[draw-perf] samples={} core_present_avg_us={} max_us={}; excludes final GXM submit/present wait",draw_samples,draw_total/draw_samples,draw_max);
        draw_report=draw_end;draw_total=draw_max=0;draw_samples=0;
    }
    host_video_present_idle();
    if (art3m1s_runtime_is_exit_requested(runtime_) && !leaving_) {
        leaving_ = true;
    }
}

} // namespace art3m1s
