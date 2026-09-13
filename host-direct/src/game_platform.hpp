#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace direct {
inline bool has_vita_section(std::string_view ini) {
    if(ini.substr(0,3)=="\xef\xbb\xbf")ini.remove_prefix(3);
    while(!ini.empty()) {
        const auto end=ini.find('\n');
        auto line=ini.substr(0,end);
        const auto first=line.find_first_not_of(" \t\r");
        if(first!=std::string_view::npos) {
            line.remove_prefix(first);
            if(line.substr(0,6)=="[VITA]") {
                line.remove_prefix(6);
                const auto tail=line.find_first_not_of(" \t\r");
                if(tail==std::string_view::npos||line[tail]==';'||line[tail]=='#')return true;
            }
        }
        if(end==std::string_view::npos)break;
        ini.remove_prefix(end+1);
    }
    return false;
}
struct GamePlatform {
    const char* name="WINDOWS";
    bool inferred=false;
    bool saved=false;
    int error=0;
};
// Called once after archives are opened. The table check uses the same virtual
// filesystem as game scripts, including loose overrides and patch archives.
template<class HasVitaTable>
GamePlatform resolve_game_platform(const std::string& directory,bool allowRecovery,
                                  std::string_view ini,HasVitaTable hasTable) {
    GamePlatform result;
    const auto path=directory+"/platform.txt";
    if(FILE* f=std::fopen(path.c_str(),"rb")) {
        char token[16]{};
        const int count=std::fscanf(f,"%15s",token);
        std::fclose(f);
        if(count==1&&(!std::strcmp(token,"VITA")||!std::strcmp(token,"vita")))result.name="VITA";
        return result; // Preserve explicit Windows, empty and unrecognized files.
    }
    if(errno!=ENOENT){result.error=errno;return result;}
    if(!allowRecovery||!has_vita_section(ini)||!hasTable())return result;
    result.name="VITA";result.inferred=true;
    // Publish only a complete marker; failed writes keep this launch on VITA
    // and will be retried on the next launch, rather than leaving an empty file.
    const auto temporary=path+".auto.tmp";
    FILE* f=std::fopen(temporary.c_str(),"wb");
    if(!f){result.error=errno;return result;}
    bool ok=std::fwrite("VITA\n",1,5,f)==5;
    if(std::fclose(f)!=0)ok=false;
    if(!ok){result.error=EIO;std::remove(temporary.c_str());return result;}
    struct stat info{};
    if(stat(path.c_str(),&info)==0) {
        std::remove(temporary.c_str());
        return resolve_game_platform(directory,allowRecovery,ini,hasTable);
    }
    if(errno!=ENOENT){result.error=errno;std::remove(temporary.c_str());return result;}
    if(std::rename(temporary.c_str(),path.c_str())==0)result.saved=true;
    else {result.error=errno;std::remove(temporary.c_str());}
    return result;
}
} // namespace direct
