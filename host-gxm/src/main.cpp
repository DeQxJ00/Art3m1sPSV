#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <algorithm>
extern "C" {
#include <libavutil/log.h>
}

namespace {
FILE* media_log = nullptr;
void media_log_callback(void* context, int level, const char* format, va_list args) {
    if (level > av_log_get_level()) return;
    char line[4096];
    thread_local int prefix = 1;
    av_log_format_line(context, level, format, args, line, sizeof(line), &prefix);
    if (media_log) {
        std::fprintf(media_log, "[ffmpeg] %s", line);
        std::fflush(media_log);
    }
}
}

#include "game_library.hpp"
#include "library_activity.hpp"
#include "media_gxm.hpp"
#include "game_surface.hpp"

extern "C" unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
extern "C" void art3m1s_gxm_finish_host_frame();
extern "C" void gxmSetPresentCompletionWait(int);
extern "C" int gxmSetAsyncPresent(int);
extern "C" void gxmSetNativePresent(int);
extern "C" void nvgxmNativeSprites(NVGcontext*, int);

int main() {
    sceIoMkdir(art3m1s::kDataRoot, 0777);
    // Some emulator/newlib combinations retain the old tail after fopen("w").
    // Rotate the previous run via native IO so this run starts with a new file.
    sceIoRemove("ux0:data/art3m1s-gxm/host.previous.log");
    const int rotated = sceIoRename("ux0:data/art3m1s-gxm/host.log", "ux0:data/art3m1s-gxm/host.previous.log");
    FILE* log = std::fopen("ux0:data/art3m1s-gxm/host.log", "w");
    if (log) {
        std::fprintf(log,"[host] file logging initialized; main_object_build %s %s; previous_log_rotate=%d\n",__DATE__,__TIME__,rotated);
        std::fflush(log);
        brls::Logger::setLogOutput(log);
        brls::Logger::setThreadSafeLogging(true);
        media_log=log;
        av_log_set_callback(media_log_callback);
    }
    brls::Logger::setLogLevel(brls::LogLevel::LOG_INFO);
    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_ZH_HANS;

    if (!brls::Application::init()) return EXIT_FAILURE;
    brls::Application::createWindow("art3m1s GXM");
#if ART3M1S_NATIVE_RENDERER
    gxmSetNativePresent(1);
    nvgxmNativeSprites(brls::Application::getNVGContext(), 1);
    brls::Logger::info("[native-renderer] reference v1: queue-then-finish, 4-vertex sprites, descriptor lookup cache, 512 atlas core; experimental");
#endif
#if ART3M1S_ASYNC_PRESENT
    brls::Logger::info("[gxm-present] async fragment completion {}; per-frame display-thread wait",
        gxmSetAsyncPresent(1) ? "enabled" : "unavailable; using full finish fallback");
#endif
#if ART3M1S_SAFE_PRESENT
    gxmSetPresentCompletionWait(1);
    brls::Logger::info("[gxm-present] completion barrier enabled; hardware flicker candidate, may reduce overlap/performance");
#endif
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::setGlobalQuit(false);
    brls::Application::pushActivity(new art3m1s::LibraryActivity());
    uint64_t report_at=sceKernelGetProcessTimeWide(), media_us=0, logic_us=0, ui_us=0, finish_us=0, max_frame_us=0;
    unsigned samples=0, slow_frames=0;
    for (;;) {
        const uint64_t t0=sceKernelGetProcessTimeWide();
        gxm_media_pump();
        const uint64_t t1=sceKernelGetProcessTimeWide();
        art3m1s::gxm_game_tick();
        const uint64_t t2=sceKernelGetProcessTimeWide();
        if (!brls::Application::mainLoop()) break;
        const uint64_t t3=sceKernelGetProcessTimeWide();
        art3m1s_gxm_finish_host_frame();
        const uint64_t t4=sceKernelGetProcessTimeWide();
        media_us+=t1-t0;logic_us+=t2-t1;ui_us+=t3-t2;finish_us+=t4-t3;
        ++samples;if(t4-t0>20000)++slow_frames;max_frame_us=std::max(max_frame_us,t4-t0);
        if(t4-report_at>=5000000) {
            brls::Logger::info("[frame-perf] frames={} media_avg_us={} logic_avg_us={} ui_present_avg_us={} capture_avg_us={} max_us={} over20ms={}; wall time includes waits",
                samples,media_us/samples,logic_us/samples,ui_us/samples,finish_us/samples,max_frame_us,slow_frames);
            SceKernelThreadInfo info={};info.size=sizeof(info);
            const int tid=sceKernelGetThreadId();
            if(sceKernelGetThreadInfo(tid,&info)>=0)
                brls::Logger::info("[main-thread] tid={} cpu={} last_cpu={} affinity={} run_clocks={}",tid,info.currentCpuId,info.lastExecutedCpuId,info.currentCpuAffinityMask,static_cast<unsigned long long>(info.runClocks));
            report_at=t4;media_us=logic_us=ui_us=finish_us=max_frame_us=0;samples=slow_frames=0;
        }
    }
    return EXIT_SUCCESS;
}
