#include "game_library.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace art3m1s {

bool valid_game_id(const char* value) {
    // Settings encode each UTF-8 byte as two hex digits (maximum 100 bytes).
    // Keep the original name intact for archives, saves and selection history.
    if (!value || !*value || std::strlen(value) > 100) return false;
    if (!std::strcmp(value, ".") || !std::strcmp(value, "..")) return false;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
        if (*p < 0x20 || *p == 0x7f || std::strchr("/\\:*?\"<>|", *p)) return false;
    }
    const char last = value[std::strlen(value) - 1];
    if (last == ' ' || last == '.') return false;
    return true;
}

std::string game_path(const std::string& id,bool bundled){
    return std::string(bundled?"app0:demos":kGamesRoot)+"/"+id;
}

namespace {

constexpr const char* bundledDemo="STARWIND_DEMO";

bool regular_file(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}
bool suffix_ci(const std::string& name,const char* suffix){
    const size_t length=std::strlen(suffix);
    return name.size()>=length&&std::equal(name.end()-length,name.end(),suffix,
        [](unsigned char a,unsigned char b){return std::tolower(a)==std::tolower(b);});
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

std::optional<uint64_t> game_folder_stamp(){
    std::vector<std::string> directories;
    DIR* root=opendir(kGamesRoot);
    if(!root&&errno!=ENOENT)return {};
    if(root){
        errno=0;
        while(const auto* entry=readdir(root)){
            if(valid_game_id(entry->d_name))directories.emplace_back(entry->d_name);
        }
        const int error=errno;
        closedir(root);
        if(error)return {};
    }
    std::sort(directories.begin(),directories.end());
    uint64_t stamp=14695981039346656037ULL;
    auto add=[&](const std::string& path){
        struct stat info{};
        if(stat(path.c_str(),&info)!=0)return errno==ENOENT;
        if(!S_ISDIR(info.st_mode))return true;
        const auto key=path+"\n"+std::to_string(info.st_ino)+":"+std::to_string(info.st_mtime)
            +":"+std::to_string(info.st_ctime)
#ifdef __linux__
            +":"+std::to_string(info.st_mtim.tv_nsec)+":"+std::to_string(info.st_ctim.tv_nsec)
#endif
            +"\n";
        for(unsigned char c:key)stamp=(stamp^c)*1099511628211ULL;
        return true;
    };
    for(const auto& id:directories)
        if(!add(game_path(id,false)))return {};
    if(!add(game_path(bundledDemo,true))||!add(std::string(kDataRoot)+"/icon-cache"))return {};
    return stamp;
}

std::vector<GameEntry> scan_games() {
    std::vector<GameEntry> games;
    DIR* root = opendir(kGamesRoot);

    while (root) {
        dirent* entry = readdir(root);
        if (!entry) break;
        if (!valid_game_id(entry->d_name)) continue;
        if (std::any_of(games.begin(), games.end(), [&](const GameEntry& g) {
                return g.id == entry->d_name;
            })) continue;
        GameEntry game;
        game.id = entry->d_name;
        game.path = game_path(game.id,false);
        DIR* directory = opendir(game.path.c_str());
        if (!directory) continue;
        std::vector<std::string> pfs_bases, exe_names;
        while (dirent* file = readdir(directory)) {
            const std::string name = file->d_name;
            if (name == "system.ini") game.has_system_ini = true;
            if (suffix_ci(name,".pfs")) {
                game.has_pfs = true;pfs_bases.push_back(name.substr(0,name.size()-4));
            }
            if (suffix_ci(name,".exe"))
                exe_names.push_back(name);
        }
        closedir(directory);
        std::sort(exe_names.begin(),exe_names.end());
        for(const auto& exe:exe_names)
            if(regular_file(game.path+"/"+exe))game.exe_candidates.push_back(game.path+"/"+exe);
        for (const auto& base : pfs_bases) for (const auto& exe : exe_names) {
            if (exe.size() != base.size()+4) continue;
            const bool same=std::equal(base.begin(),base.end(),exe.begin(),[](unsigned char a,unsigned char b){
                return std::tolower(a)==std::tolower(b);
            });
            if (same && regular_file(game.path+"/"+exe)) game.matching_exe=game.path+"/"+exe;
        }
        game.title = read_title(game.path + "/title.txt", game.id);
        games.push_back(std::move(game));
    }
    if (root) closedir(root);

    // Shipped original demos stay read-only in app0; saves and settings use
    // the normal per-game ux0 directories. An external copy takes precedence.
    for (const char* id : {bundledDemo}) {
        if (std::any_of(games.begin(), games.end(), [&](const GameEntry& g) {
                return g.id == id;
            })) continue;
        GameEntry game;
        game.id = id;
        game.path = game_path(id,true);
        game.has_pfs = regular_file(game.path + "/root.pfs");
        if (!game.has_pfs) continue;
        game.title = read_title(game.path + "/title.txt", game.id);
        game.bundled = true;
        games.push_back(std::move(game));
    }

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
