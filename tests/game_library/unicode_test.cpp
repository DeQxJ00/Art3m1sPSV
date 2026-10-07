#include "game_library.hpp"
#include "font_settings.hpp"
#include <algorithm>
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
    for (const auto* id : {"STARWIND_DEMO", "STARWIND_EMOTE"}) {
        const auto path = std::string("app0:demos/") + id;
        fs::create_directories(path);
        std::ofstream(path + "/root.pfs") << "fixture";
    }
    auto games = art3m1s::scan_games();
    assert(games.size() == 1 && games[0].id == "STARWIND_DEMO" && games[0].bundled && games[0].ready());
    const std::string root = art3m1s::kGamesRoot;
    fs::create_directories(root);
    const std::vector<std::string> names = {
        "Test_Game-01", "中文游戏（测试）", "日本語 テスト [完全版]",
        "Title & Friends + #1! 'Edition'", "标题.v2", std::string(100, 'a'),
        "STARWIND_DEMO"
    };
    for (const auto& name : names) {
        fs::create_directories(root + "/" + name);
        std::ofstream(root + "/" + name + "/system.ini") << "[VITA]\n";
        std::ofstream(root + "/" + name + "/资源.pfs") << "fixture";
        std::ofstream(root + "/" + name + "/资源.exe") << "fixture";
    }
    for (const auto& name : {std::string("bad:name"), std::string("bad\\name"),
                            std::string("bad\nname"), std::string("bad?name"),
                            std::string("trailing."), std::string("trailing "),
                            std::string(101, 'a')}) {
        fs::create_directories(root + "/" + name);
    }
    games = art3m1s::scan_games();
    assert(games.size() == names.size());
    fs::create_directories("settings");
    for (const auto& name : names) {
        const auto found = std::find_if(games.begin(), games.end(), [&](const auto& game) {
            return game.id == name;
        });
        assert(found != games.end() && !found->bundled && found->ready());
        assert(found->title == name && found->path == root + "/" + name);
        assert(found->matching_exe == found->path + "/资源.exe");
        assert(found->exe_candidates == std::vector<std::string>{found->path + "/资源.exe"});
        std::ifstream ini(found->path + "/system.ini");
        std::string line; std::getline(ini, line); assert(line == "[VITA]");
        assert(art3m1s::save_last_game(name));
        assert(art3m1s::load_last_game() == name);
        const auto settings = direct::font_settings_path("settings", name);
        direct::FontSettings value; value.enabled = true; value.dialogue = 125;
        assert(direct::save_font_settings(settings, value));
        assert(direct::load_font_settings(settings).dialogue == 125);
        const auto saves = std::string(art3m1s::kDataRoot) + "/saves/" + name;
        fs::create_directories(saves);
        std::ofstream(saves + "/slot.dat") << "roundtrip";
        std::ifstream slot(saves + "/slot.dat");
        std::getline(slot, line); assert(line == "roundtrip");
    }
    assert(direct::font_settings_path("settings", "中文") !=
           direct::font_settings_path("settings", "日本語"));
    const auto changed=root+"/Test_Game-01";
    fs::rename(changed+"/资源.exe",changed+"/Different Name.EXE");
    std::ofstream(changed+"/helper.exe")<<"no icon";
    fs::create_directory(changed+"/directory.exe");
    games=art3m1s::scan_games();
    const auto changedGame=std::find_if(games.begin(),games.end(),[](const auto& g){return g.id=="Test_Game-01";});
    assert(changedGame!=games.end()&&changedGame->matching_exe.empty());
    assert((changedGame->exe_candidates==std::vector<std::string>{changed+"/Different Name.EXE",changed+"/helper.exe"}));
    fs::current_path(previous);
    fs::remove_all(temp);
    std::cout << "PASS UTF-8/symbol names, safety, archives, settings, save paths, selection, bundled precedence\n";
}
