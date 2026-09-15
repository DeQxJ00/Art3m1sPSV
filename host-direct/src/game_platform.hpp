#pragma once
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace direct {
inline std::string_view ini_section(std::string_view ini,std::string_view name) {
    if(ini.substr(0,3)=="\xef\xbb\xbf")ini.remove_prefix(3);
    bool found=false;size_t start=0,offset=0;
    const auto original=ini;
    while(!ini.empty()) {
        const auto end=ini.find_first_of("\r\n");
        auto line=ini.substr(0,end);
        const auto first=line.find_first_not_of(" \t\r");
        if(first!=std::string_view::npos) {
            line.remove_prefix(first);
            if(line[0]=='['&&line.find(']')!=std::string_view::npos) {
                const auto close=line.find(']');
                auto section=line.substr(1,close-1);
                const auto begin=section.find_first_not_of(" \t");
                section=begin==std::string_view::npos?std::string_view{}:
                    section.substr(begin,section.find_last_not_of(" \t")-begin+1);
                bool matches=section.size()==name.size();
                for(size_t i=0;matches&&i<section.size();++i) {
                    char c=section[i];if(c>='a'&&c<='z')c-=32;
                    matches=c==name[i];
                }
                line.remove_prefix(close+1);
                const auto tail=line.find_first_not_of(" \t\r");
                if(tail==std::string_view::npos||line[tail]==';'||line[tail]=='#') {
                    if(found)return original.substr(start,offset-start);
                    if(matches){found=true;start=offset;}
                }
            }
        }
        if(end==std::string_view::npos)break;
        ini.remove_prefix(end+1);
        offset+=end+1;
    }
    return found?original.substr(start):std::string_view{};
}
inline bool has_vita_section(std::string_view ini){return !ini_section(ini,"VITA").empty();}
// Preserve source bytes (including legacy encodings) and user dimensions.
// This compatibility section exists only in the startup buffer, not in the PFS.
inline bool add_vita_section(std::string& ini) {
    if(has_vita_section(ini))return false;
    for(const auto source:{"WINDOWS","ANDROID","IOS","SWITCH","PS4","WASM"}) {
        const auto section=ini_section(ini,source);
        if(section.empty())continue;
        const auto end=section.find_first_of("\r\n");
        if(end==std::string_view::npos)continue;
        const std::string copy(section.substr(end));
        ini+="\n[VITA]";ini+=copy;return true;
    }
    return false;
}
struct GamePlatform {
    const char* name="VITA";
    bool inferred=false;
    bool saved=false;
    int error=0;
};
// Resolve the platform before probing platform-specific tables. Bundled demos
// use the same default but cannot write into the read-only application assets.
inline GamePlatform resolve_game_platform(const std::string& directory,bool persist=true) {
    GamePlatform result;
    const auto path=directory+"/platform.txt";
    if(FILE* f=std::fopen(path.c_str(),"rb")) {
        char token[16]{};
        const int count=std::fscanf(f,"%15s",token);
        std::fclose(f);
        if(count==1) {
            for(char* p=token;*p;++p)if(*p>='a'&&*p<='z')*p-=32;
            if(!std::strcmp(token,"WINDOWS"))result.name="WINDOWS";
        }
        return result; // Preserve the file; VITA/psvita and unknown tokens use VITA.
    }
    if(errno!=ENOENT){result.error=errno;return result;}
    result.name="VITA";result.inferred=true;
    if(!persist)return result;
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
        return resolve_game_platform(directory,persist);
    }
    if(errno!=ENOENT){result.error=errno;std::remove(temporary.c_str());return result;}
    if(std::rename(temporary.c_str(),path.c_str())==0)result.saved=true;
    else {result.error=errno;std::remove(temporary.c_str());}
    return result;
}
} // namespace direct
