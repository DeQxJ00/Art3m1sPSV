#include "game_library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace art3m1s {
namespace {

bool valid_id(const char* value) {
    if (!value || !*value || std::strlen(value) >= 64) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (!std::isalnum(*p) && *p != '-' && *p != '_') return false;
    }
    return true;
}

bool regular_file(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

std::string read_title(const std::string& path, const std::string& fallback) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return fallback;
    char value[512] {};
    const bool ok = std::fgets(value, sizeof(value), file) != nullptr;
    std::fclose(file);
    if (!ok) return fallback;
    value[std::strcspn(value, "\r\n")] = 0;
    return *value ? std::string(value) : fallback;
}

int acceptance_priority(const std::string& id) {
    static const char* ordered[] = {
        "SHUF00002", "PCSG01297", "PCSG01201",
        "PCSG01107", "PCSG01084", "PCSG01127",
    };
    for (int i = 0; i < 6; ++i) if (id == ordered[i]) return i;
    return 100;
}

} // namespace

std::vector<GameEntry> scan_games() {
    std::vector<GameEntry> games;
#ifdef DIRECT_BUNDLED_SHADER_DEMO
    GameEntry demo;
    demo.id = "TEST_SHADERS_51";
    demo.path = "app0:/demos/TEST_SHADERS_51";
    demo.title = "TEST SHADERS 51";
    demo.has_system_ini = regular_file(demo.path + "/system.ini");
    demo.bundled = true;
    if (demo.ready()) games.push_back(std::move(demo));
#endif
    DIR* root = opendir(kGamesRoot);

    while (root) {
        dirent* entry = readdir(root);
        if (!entry) break;
        if (!valid_id(entry->d_name)) continue;
        if (std::any_of(games.begin(), games.end(), [&](const GameEntry& g) {
                return g.id == entry->d_name;
            })) continue;
        GameEntry game;
        game.id = entry->d_name;
        game.path = std::string(kGamesRoot) + "/" + game.id;
        DIR* directory = opendir(game.path.c_str());
        if (!directory) continue;
        while (dirent* file = readdir(directory)) {
            const std::string name = file->d_name;
            if (name == "system.ini") game.has_system_ini = true;
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".pfs") == 0) game.has_pfs = true;
        }
        closedir(directory);
        game.title = read_title(game.path + "/title.txt", game.id);
        games.push_back(std::move(game));
    }
    if (root) closedir(root);

    std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
        const int ap = acceptance_priority(a.id);
        const int bp = acceptance_priority(b.id);
        return ap == bp ? a.id < b.id : ap < bp;
    });
    return games;
}

std::string load_last_game() {
    const std::string path = std::string(kDataRoot) + "/last-game.txt";
    return read_title(path, "");
}

bool save_last_game(const std::string& id) {
    const std::string path = std::string(kDataRoot) + "/last-game.txt";
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    const bool ok = std::fwrite(id.data(), 1, id.size(), file) == id.size()
        && std::fwrite("\n", 1, 1, file) == 1;
    return std::fclose(file) == 0 && ok;
}

} // namespace art3m1s
