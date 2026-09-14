#include "game_library.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

int main() {
    namespace fs = std::filesystem;
    char temp[] = "/tmp/art3-library-XXXXXX";
    assert(mkdtemp(temp));
    const auto previous = fs::current_path();
    fs::current_path(temp);
    fs::create_directories("app0:/demos/TEST_SHADERS_51");
    std::ofstream("app0:/demos/TEST_SHADERS_51/system.ini") << "[VITA]\n";
    std::ofstream("app0:/demos/TEST_SHADERS_51/title.txt") << "内置 Shader 演示 demo\n";
    fs::create_directories("app0:/demos/TEST_SHADERS_EXTERNAL");
    std::ofstream("app0:/demos/TEST_SHADERS_EXTERNAL/system.ini") << "[VITA]\n";
    std::ofstream("app0:/demos/TEST_SHADERS_EXTERNAL/title.txt") << "外置 Shader 演示 demo\n";
    auto games = art3m1s::scan_games();
    assert(games.size() == 2 && games[0].bundled && games[0].ready());
    assert(games[0].path == "app0:/demos/TEST_SHADERS_51");
    assert(games[0].title == "内置 Shader 演示 demo");
    assert(games[1].title == "外置 Shader 演示 demo" && games[1].bundled);

    fs::create_directories("ux0:data/art3m1s-gxm/games/TEST_SHADERS_51");
    std::ofstream("ux0:data/art3m1s-gxm/games/TEST_SHADERS_51/system.ini") << "old demo";
    fs::create_directories("ux0:data/art3m1s-gxm/games/SHUF00002");
    std::ofstream("ux0:data/art3m1s-gxm/games/SHUF00002/system.ini") << "real game";
    games = art3m1s::scan_games();
    assert(games.size() == 3);
    assert(games[0].id == "SHUF00002" && !games[0].bundled);
    assert(games[1].id == "TEST_SHADERS_51" && games[1].bundled);
    assert(art3m1s::save_last_game(games[2].id));
    assert(art3m1s::load_last_game() == "TEST_SHADERS_EXTERNAL");

    fs::remove("app0:/demos/TEST_SHADERS_51/system.ini");
    games = art3m1s::scan_games();
    assert(games.size() == 3 && !games[1].bundled && games[1].ready());
    assert(games[2].bundled);
    fs::current_path(previous);
    fs::remove_all(temp);
    std::cout << "PASS clean install, duplicate preference, priority, selection, external fallback\n";
}
