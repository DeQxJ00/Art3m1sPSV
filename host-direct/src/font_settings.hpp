#pragma once
#include <cstdio>
#include <string>

namespace direct {
struct FontSettings {
    bool enabled=false;
    unsigned name=100,dialogue=100;
};
inline bool parse_font_settings(const char* text,FontSettings& out) {
    unsigned version,on,name,dialogue;char trailing;
    if(std::sscanf(text,"%u %u %u %u %c",&version,&on,&name,&dialogue,&trailing)!=4 ||
        version!=1||on>1||name<75||name>150||dialogue<75||dialogue>150)return false;
    out={on!=0,name,dialogue};return true;
}
inline std::string font_settings_path(const std::string& directory,const std::string& id) {
    // Reversible encoding prevents traversal and collisions between game IDs.
    if(id.empty()||id.size()>100)return {};
    const char* hex="0123456789abcdef";std::string path=directory+"/";
    for(unsigned char c:id){path+=hex[c>>4];path+=hex[c&15];}
    return path+".font";
}
inline FontSettings load_font_settings(const std::string& path) {
    FontSettings value;if(path.empty())return value;
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(!f)return value;
    char text[96]{};size_t n=std::fread(text,1,sizeof(text)-1,f);int extra=std::fgetc(f);std::fclose(f);
    if(extra==EOF&&n)parse_font_settings(text,value);return value;
}
inline bool save_font_settings(const std::string& path,const FontSettings& value) {
    if(path.empty()||value.name<75||value.name>150||value.dialogue<75||value.dialogue>150)return false;
    const auto tmp=path+".tmp",bak=path+".bak";
    FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"1 %u %u %u\n",unsigned(value.enabled),value.name,value.dialogue)>0;
    if(std::fclose(f)!=0)ok=false;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())!=0){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())!=0){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
