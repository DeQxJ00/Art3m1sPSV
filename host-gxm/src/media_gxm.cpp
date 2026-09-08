#include "media_gxm.hpp"
extern "C" {
#include "audio.h"
#include "video.h"
#include "files.h"
}
#include <deque>
#include <string>
#include <utility>
#include <cstdio>
#include "thread_perf.h"

namespace {
void* active_runtime = nullptr;
bool closing = false, audio_started = false, skip_requested = false;
std::deque<std::pair<std::string, std::string>> commands;
}
void gxm_media_attach(void* runtime) { active_runtime = runtime; }
void gxm_media_command(const char* kind, const char* json) {
    if (active_runtime && kind && json) commands.emplace_back(kind, json);
}
void gxm_media_skip() { skip_requested = true; }
void gxm_media_detach() {
    active_runtime = nullptr;
    closing = true;
    commands.clear();
    skip_requested = false;
}
void gxm_media_pump() {
    static HostThreadPerf thread_perf={};host_thread_perf("main",&thread_perf,0);
    // Invoked before mainLoop begins its scene. Decoder release can therefore
    // wait for GXM without deadlocking against the current display submission.
    if (closing) {
        host_video_close();
        host_audio_stop();
        audio_started = false;
        host_files_close();
        closing = false;
    }
    if (!active_runtime) return;
    if (!audio_started) {
        int result = host_audio_start();
        if (result != 0) { std::printf("[media] audio worker start failed: %d\n", result); return; }
        audio_started = true;
    }
    while (!commands.empty()) {
        auto command = std::move(commands.front());
        commands.pop_front();
        host_media_command(command.first.c_str(), command.second.c_str());
    }
    if (skip_requested) { host_video_skip(active_runtime); skip_requested = false; }
    host_audio_poll(active_runtime);
    host_video_tick(active_runtime);
}
