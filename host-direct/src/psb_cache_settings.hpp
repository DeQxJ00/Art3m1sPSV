#pragma once
#include "font_settings.hpp"
#include <cerrno>
namespace direct {
struct PsbCacheSettings{unsigned groups=4,mib=32;};
inline bool parse_psb_cache_settings(const char* text,PsbCacheSettings& out){
    unsigned version,groups,mib;char extra;
    if(std::sscanf(text,"%u %u %u %c",&version,&groups,&mib,&extra)!=3||version!=1||groups<1||groups>16||mib<8||mib>128)return false;
    out={groups,mib};return true;
}
inline std::string psb_cache_settings_path(const std::string& dir,const std::string& id){auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".psb-cache");return p;}
inline PsbCacheSettings load_psb_cache_settings(const std::string& path){
    PsbCacheSettings value;if(path.empty())return value;
    for(const auto& file:{path,path+".bak"}){FILE* f=std::fopen(file.c_str(),"rb");if(!f)continue;
        char text[96]{};size_t n=std::fread(text,1,sizeof(text)-1,f);bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
        if(ok&&parse_psb_cache_settings(text,value))return value;}
    return value;
}
inline bool save_psb_cache_settings(const std::string& path,const PsbCacheSettings& value){
    if(path.empty())return false;char text[80];std::snprintf(text,sizeof(text),"1 %u %u\n",value.groups,value.mib);
    PsbCacheSettings check;if(!parse_psb_cache_settings(text,check))return false;
    const auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fputs(text,f)>=0;if(std::fclose(f)!=0)ok=false;if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}else if(errno!=ENOENT){std::remove(tmp.c_str());return false;}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
