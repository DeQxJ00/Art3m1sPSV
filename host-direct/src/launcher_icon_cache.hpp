#pragma once
#include "game_library.hpp"
#include "runtime_api.h"
#include <array>
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
};
static_assert(sizeof(LauncherIconCacheHeader)==24);
inline constexpr char launcherIconMagic[8]={'A','3','I','C','O','N','0','1'};

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
    if(game.matching_exe.empty())return false;
    std::array<uint8_t,launcherIconBytes> rgba{};
    if(launcher_icon_read_png(game,rgba))return false;
    struct stat st{};
    if(::stat(game.matching_exe.c_str(),&st)!=0||!S_ISREG(st.st_mode)
        ||st.st_size<64||st.st_size>32*1024*1024)return false;
    const auto size=static_cast<uint64_t>(st.st_size),time=static_cast<uint64_t>(st.st_mtime);
    LauncherIconCacheHeader old{};
    if(launcher_icon_read_cache(game,rgba,&old,cacheRoot)&&old.sourceSize==size&&old.sourceTime==time)
        return true;
    FILE* exe=std::fopen(game.matching_exe.c_str(),"rb");if(!exe)return false;
    std::vector<uint8_t> bytes(size_t(size),0);
    const bool read=std::fread(bytes.data(),1,bytes.size(),exe)==bytes.size();
    std::fclose(exe);
    if(!read||!art3m1s_launcher_extract_exe_icon(bytes.data(),bytes.size(),rgba.data(),rgba.size()))return false;
    std::vector<uint8_t>().swap(bytes);
    const std::string directory=std::string(cacheRoot)+"/icon-cache";
    if(::mkdir(directory.c_str(),0777)!=0){
        struct stat dir{};
        if(::stat(directory.c_str(),&dir)!=0||!S_ISDIR(dir.st_mode))return false;
    }
    LauncherIconCacheHeader current{};
    std::memcpy(current.magic,launcherIconMagic,sizeof(current.magic));
    current.sourceSize=size;current.sourceTime=time;
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
