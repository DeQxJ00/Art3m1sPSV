#pragma once
#include <cstdint>

namespace direct {
// The fixed fallback menu is independent of the launcher's dynamic font cache.
enum class FallbackLabel : unsigned { Title, Help, Save, Load, QuickSave, QuickLoad,
    Config, Backlog, Auto, Return, FontTitle, FontEnable, FontName, FontDialogue,
    FontReset, FontSave, FontOn, FontOff, FontNote, FontHelp, FontError,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Percent, FontEntry, ExitGame, Count };
bool fallback_menu_prepare();
void fallback_menu_text(float x, float baseline, FallbackLabel label, uint32_t color=0xffffffff);
void fallback_menu_release();
}
