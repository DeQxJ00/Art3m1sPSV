#pragma once

#include <borealis.hpp>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <pthread.h>
#include <atomic>

#include "game_library.hpp"

namespace art3m1s {

void gxm_game_tick();

class GameSurface final : public brls::View {
public:
    explicit GameSurface(GameEntry game);
    ~GameSurface() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* context) override;
    brls::View* getDefaultFocus() override { return this; }
    bool loaded() const { return loaded_; }
    void tick();
    const std::string& error() const { return error_; }

private:
    GameEntry game_;
    void* runtime_ = nullptr;
    bool loaded_ = false;
    bool leaving_ = false;
    bool menu_fonts_released_ = false;
    bool trace_nextline_ = false;
    uint64_t trace_since_ = 0, trace_logic_max_ = 0, trace_prepare_max_ = 0;
    uint64_t trace_slow_count_ = 0;
    std::string error_;
    uint64_t previous_time_ = 0;
    uint32_t previous_buttons_ = 0;
    SceTouchData previous_touch_ {};
    int mouse_x_ = 480;
    int mouse_y_ = 272;
    pthread_t loading_thread_ {};
    bool loading_thread_started_ = false;
    std::atomic<int> archive_result_ {-999};
    int loading_stage_ = 0;
    static void* load_archives(void* surface);

    void initialize();
    void feed_input();
};

} // namespace art3m1s
