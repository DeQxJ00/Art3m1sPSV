#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdlib>
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
// Preserve source bytes (including legacy encodings) and existing Vita settings.
// This compatibility section exists only in the startup buffer, not in the PFS.
inline bool add_vita_section(std::string& ini) {
    if(has_vita_section(ini))return false;
    for(const auto source:{"WINDOWS","ANDROID","IOS","SWITCH","PS4","WASM"}) {
        const auto section=ini_section(ini,source);
        if(section.empty())continue;
        const auto end=section.find_first_of("\r\n");
        if(end==std::string_view::npos)continue;
        const std::string copy(section.substr(end));
        ini+="\n[VITA]";ini+=copy;
        ini+="\nWIDTH=960\nHEIGHT=540\n";return true;
    }
    return false;
}
struct GamePlatform {
    const char* name="VITA";
    bool inferred=false;
    int error=0;
};
// Resolve the platform before probing platform-specific tables. Bundled demos
// use the same default but cannot write into the read-only application assets.
inline GamePlatform resolve_game_platform(const std::string& directory) {
    GamePlatform result;
    const auto path=directory+"/platform.txt";
    FILE* marker=std::fopen(path.c_str(),"rb");
    if(!marker&&errno==ENOENT)marker=std::fopen((path+".bak").c_str(),"rb");
    if(FILE* f=marker) {
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
    result.inferred=true;return result; // Missing marker is the default, not a write request.
}
// Only explicit menu changes create/replace platform.txt. Keep the old choice
// recoverable if publishing the new file fails on the Vita filesystem.
inline bool save_game_platform(const std::string& directory,bool windows) {
    const auto path=directory+"/platform.txt",temporary=path+".tmp",backup=path+".bak";
    FILE* f=std::fopen(temporary.c_str(),"wb");if(!f)return false;
    bool ok=std::fputs(windows?"WINDOWS\n":"VITA\n",f)>=0;
    if(std::fclose(f)!=0)ok=false;
    if(!ok){std::remove(temporary.c_str());return false;}
    struct stat info{};const bool existed=stat(path.c_str(),&info)==0;
    if(existed){
        if(!S_ISREG(info.st_mode)){std::remove(temporary.c_str());return false;}
        std::remove(backup.c_str());
        if(std::rename(path.c_str(),backup.c_str())!=0){std::remove(temporary.c_str());return false;}
    }else if(errno!=ENOENT){std::remove(temporary.c_str());return false;}
    if(std::rename(temporary.c_str(),path.c_str())!=0){
        if(existed)std::rename(backup.c_str(),path.c_str());
        std::remove(temporary.c_str());return false;
    }
    std::remove(backup.c_str());return true;
}
struct VitaResolution {unsigned width=960,height=540;};
inline VitaResolution vita_resolution(std::string_view ini) {
    VitaResolution out;
    auto section=ini_section(ini,"VITA");
    while(!section.empty()){
        auto end=section.find_first_of("\r\n");auto line=section.substr(0,end);
        auto equal=line.find('=');
        if(equal!=line.npos){
            auto key=line.substr(0,equal);auto begin=key.find_first_not_of(" \t");
            if(begin!=key.npos){
                std::string name(key.substr(begin,key.find_last_not_of(" \t")-begin+1));
                for(char& c:name)if(c>='a'&&c<='z')c-=32;
                if(name=="WIDTH"||name=="HEIGHT"){
                    std::string value(line.substr(equal+1));char* tail=nullptr;
                    const auto n=std::strtoul(value.c_str(),&tail,10);
                    while(tail&&(*tail==' '||*tail=='\t'))++tail;
                    if(tail&&tail!=value.c_str()&&!*tail&&n>0&&n<=8192)
                        (name=="WIDTH"?out.width:out.height)=unsigned(n);
                }
            }
        }
        if(end==section.npos)break;section.remove_prefix(end+1);
    }
    return out;
}
} // namespace direct
