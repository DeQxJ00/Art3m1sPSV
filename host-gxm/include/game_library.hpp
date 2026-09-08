#pragma once

#include <string>
#include <vector>

namespace art3m1s {

inline constexpr const char* kDataRoot = "ux0:data/art3m1s-gxm";
inline constexpr const char* kGamesRoot = "ux0:data/art3m1s-gxm/games";

struct GameEntry {
    std::string id;
    std::string title;
    std::string path;
    bool has_system_ini = false;
    bool has_pfs = false;

    bool ready() const { return has_system_ini || has_pfs; }
};

std::vector<GameEntry> scan_games();
std::string load_last_game();
bool save_last_game(const std::string& id);

} // namespace art3m1s
