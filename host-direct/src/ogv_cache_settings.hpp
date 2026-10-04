#pragma once
#include "font_settings.hpp"
#include <cerrno>
namespace direct {
struct OgvCacheSettings{bool enabled=true;unsigned groups=4,mib=16;};
inline bool parse_ogv_cache_settings(const char* text,OgvCacheSettings& out){
    unsigned version,on,groups,mib;char extra;
    if(std::sscanf(text,"%u %u %u %u %c",&version,&on,&groups,&mib,&extra)!=4||version!=1||on>1||groups<1||groups>16||mib<4||mib>64)return false;
    out={on!=0,groups,mib};return true;
}
inline std::string ogv_cache_settings_path(const std::string& dir,const std::string& id){auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".ogv-cache");return p;}
inline OgvCacheSettings load_ogv_cache_settings(const std::string& path){
    OgvCacheSettings value;if(path.empty())return value;
    for(const auto& file:{path,path+".bak"}){FILE* f=std::fopen(file.c_str(),"rb");if(!f)continue;
        char text[96]{};size_t n=std::fread(text,1,sizeof(text)-1,f);bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
        if(ok&&parse_ogv_cache_settings(text,value))return value;}
    return value;
}
inline bool save_ogv_cache_settings(const std::string& path,const OgvCacheSettings& value){
    if(path.empty())return false;char text[80];std::snprintf(text,sizeof(text),"1 %u %u %u\n",unsigned(value.enabled),value.groups,value.mib);
    OgvCacheSettings check;if(!parse_ogv_cache_settings(text,check))return false;
    const auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fputs(text,f)>=0;if(std::fclose(f)!=0)ok=false;if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}else if(errno!=ENOENT){std::remove(tmp.c_str());return false;}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
