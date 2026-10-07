#pragma once

namespace direct {
// Shared by dynamic launcher text and the baked in-game menu atlas.
inline constexpr int kMenuTitleSize = 28;
inline constexpr int kMenuBodySize = 24;
inline constexpr int kMenuNoteSize = 20;
// Shared optical alignment for dynamic text and the baked host-menu atlas.
inline constexpr float kMenuTextOffsetY = -1.0f;
// The game selection screen retains its original text baselines.
inline float menuTextOffsetY = kMenuTextOffsetY;
}
