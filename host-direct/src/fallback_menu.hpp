#pragma once
#include <cstdint>

namespace direct {
// The fixed fallback menu is independent of the launcher's dynamic font cache.
enum class FallbackLabel : unsigned { Title, Help, Save, Load, QuickSave, QuickLoad,
    Config, Backlog, Auto, Return, Count };
bool fallback_menu_prepare();
void fallback_menu_text(float x, float baseline, FallbackLabel label, uint32_t color=0xffffffff);
void fallback_menu_release();
}
