#pragma once
#include "game_library.hpp"
#include "title_initial.hpp"
#include <algorithm>

namespace direct {
inline std::string library_title_key(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
inline void sort_library(std::vector<art3m1s::GameEntry>& games, size_t& selected, bool descending) {
    const std::string current = selected < games.size() ? games[selected].id : std::string();
    std::sort(games.begin(), games.end(), [descending](const auto& a, const auto& b) {
        const char ai = title_initial(a.title), bi = title_initial(b.title);
        // Unclassified titles stay at the end in either direction.
        if ((ai == '#') != (bi == '#')) return bi == '#';
        if (ai != bi) return descending ? ai > bi : ai < bi;
        const auto ak = library_title_key(a.title), bk = library_title_key(b.title);
        if (ak != bk) return descending ? ak > bk : ak < bk;
        return a.id < b.id;
    });
    selected = 0;
    for (size_t i = 0; i < games.size(); ++i) if (games[i].id == current) { selected = i; break; }
}
}
