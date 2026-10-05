#pragma once
#include "game_library.hpp"
#include "runtime_api.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace direct {
constexpr size_t launcherIconSide=48,launcherIconBytes=launcherIconSide*launcherIconSide*4;
struct LauncherIconCacheHeader {
    char magic[8];
    uint64_t sourceSize;
    uint64_t sourceTime;
    uint64_t candidatesHash;
};
static_assert(sizeof(LauncherIconCacheHeader)==32);
inline constexpr char launcherIconMagic[8]={'A','3','I','C','O','N','0','2'};

inline std::string launcher_icon_cache_path(const art3m1s::GameEntry& game,
    const char* cacheRoot=art3m1s::kDataRoot){
    return std::string(cacheRoot)+"/icon-cache/"+game.id+".rgba";
}
inline bool launcher_icon_read_png(const art3m1s::GameEntry& game,
    std::array<uint8_t,launcherIconBytes>& rgba){
    for(const char* name:{"icon.png","icon0.png","sce_sys/icon0.png","saveicon.png"}){
        FILE* file=std::fopen((game.path+"/"+name).c_str(),"rb");if(!file)continue;
        if(std::fseek(file,0,SEEK_END)!=0){std::fclose(file);continue;}
        const long length=std::ftell(file);
        if(length<8||length>2*1024*1024){std::fclose(file);continue;}
        std::rewind(file);
        std::vector<uint8_t> png(size_t(length),0);
        const bool read=std::fread(png.data(),1,png.size(),file)==png.size();
        std::fclose(file);
        if(read&&art3m1s_launcher_decode_icon(png.data(),png.size(),rgba.data(),rgba.size()))return true;
    }
    return false;
}
inline bool launcher_icon_read_cache(const art3m1s::GameEntry& game,
    std::array<uint8_t,launcherIconBytes>& rgba,LauncherIconCacheHeader* header=nullptr,
    const char* cacheRoot=art3m1s::kDataRoot){
    FILE* file=std::fopen(launcher_icon_cache_path(game,cacheRoot).c_str(),"rb");if(!file)return false;
    LauncherIconCacheHeader value{};unsigned char extra=0;
    const bool valid=std::fread(&value,1,sizeof(value),file)==sizeof(value)
        &&std::memcmp(value.magic,launcherIconMagic,sizeof(value.magic))==0
        &&std::fread(rgba.data(),1,rgba.size(),file)==rgba.size()
        &&std::fread(&extra,1,1,file)==0;
    std::fclose(file);if(valid&&header)*header=value;return valid;
}
inline bool launcher_icon_generate_cache(const art3m1s::GameEntry& game,
    const char* cacheRoot=art3m1s::kDataRoot){
    std::array<uint8_t,launcherIconBytes> rgba{};
    if(launcher_icon_read_png(game,rgba))return false;
    // Discovery only records names. PE parsing stays in the game-loading worker.
    // A matching EXE always takes precedence, even if its icon cannot be decoded.
    auto sources=game.matching_exe.empty()?game.exe_candidates:std::vector<std::string>{game.matching_exe};
    std::sort(sources.begin(),sources.end());
    sources.erase(std::unique(sources.begin(),sources.end()),sources.end());
    auto invalidate=[&](){std::remove(launcher_icon_cache_path(game,cacheRoot).c_str());return false;};
    if(sources.empty())return invalidate();
    std::vector<struct stat> stats(sources.size());
    uint64_t signature=14695981039346656037ULL;
    for(size_t i=0;i<sources.size();++i){
        if(::stat(sources[i].c_str(),&stats[i])!=0||!S_ISREG(stats[i].st_mode)
            ||stats[i].st_size<0||stats[i].st_size>32*1024*1024)return invalidate();
        // Include all names and metadata: adding another icon-bearing EXE must
        // invalidate uniqueness, including equal-sized/equal-time replacements.
        const auto key=sources[i]+"\n"+std::to_string(stats[i].st_size)+":"+std::to_string(stats[i].st_mtime)+"\n";
        for(unsigned char c:key)signature=(signature^c)*1099511628211ULL;
    }
    LauncherIconCacheHeader old{};
    if(launcher_icon_read_cache(game,rgba,&old,cacheRoot)&&old.candidatesHash==signature)return true;
    size_t found=0,selected=0;
    for(size_t i=0;i<sources.size();++i){
        if(stats[i].st_size<64)continue;
        FILE* exe=std::fopen(sources[i].c_str(),"rb");if(!exe)return invalidate();
        std::vector<uint8_t> bytes(size_t(stats[i].st_size),0);
        const bool read=std::fread(bytes.data(),1,bytes.size(),exe)==bytes.size();
        std::fclose(exe);if(!read)return invalidate();
        std::array<uint8_t,launcherIconBytes> candidate{};
        if(!art3m1s_launcher_extract_exe_icon(bytes.data(),bytes.size(),candidate.data(),candidate.size()))continue;
        if(++found>1)return invalidate();
        rgba=candidate;selected=i;
    }
    if(found!=1)return invalidate();
    const std::string directory=std::string(cacheRoot)+"/icon-cache";
    if(::mkdir(directory.c_str(),0777)!=0){
        struct stat dir{};
        if(::stat(directory.c_str(),&dir)!=0||!S_ISDIR(dir.st_mode))return false;
    }
    LauncherIconCacheHeader current{};
    std::memcpy(current.magic,launcherIconMagic,sizeof(current.magic));
    current.sourceSize=static_cast<uint64_t>(stats[selected].st_size);
    current.sourceTime=static_cast<uint64_t>(stats[selected].st_mtime);
    current.candidatesHash=signature;
    const std::string path=launcher_icon_cache_path(game,cacheRoot),temporary=path+".tmp";
    FILE* cache=std::fopen(temporary.c_str(),"wb");if(!cache)return false;
    const bool headerWritten=std::fwrite(&current,1,sizeof(current),cache)==sizeof(current);
    const bool pixelsWritten=headerWritten&&std::fwrite(rgba.data(),1,rgba.size(),cache)==rgba.size();
    const bool closed=std::fclose(cache)==0;
    const bool written=headerWritten&&pixelsWritten&&closed;
    if(!written){std::remove(temporary.c_str());return false;}
    if(std::rename(temporary.c_str(),path.c_str())!=0){
        std::remove(path.c_str());
        if(std::rename(temporary.c_str(),path.c_str())!=0){std::remove(temporary.c_str());return false;}
    }
    return true;
}
}
