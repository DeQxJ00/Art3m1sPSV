#pragma once
#include "font_settings.hpp"
#include <cerrno>
namespace direct {
struct ImageCacheSettings { unsigned luaCount=128,readyMib=64,idleMib=96; };
inline bool parse_image_cache_settings(const char* text,ImageCacheSettings& out){
    unsigned version=0,count=0,ready=0,idle=0;char extra;
    if(!text||std::sscanf(text,"%u %u %u %u %c",&version,&count,&ready,&idle,&extra)!=4)return false;
    if(version!=1||count<16||count>4096||ready<8||ready>192||idle<8||idle>192)return false;
    out={count,ready,idle};return true;
}
inline std::string image_cache_settings_path(const std::string& dir,const std::string& id){auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".image-cache");return p;}
inline ImageCacheSettings load_image_cache_settings(const std::string& path){
    ImageCacheSettings value;if(path.empty())return value;
    for(const auto& file:{path,path+".bak"}){FILE* f=std::fopen(file.c_str(),"rb");if(!f)continue;
        char text[96]{};size_t n=std::fread(text,1,sizeof(text)-1,f);bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
        if(ok&&parse_image_cache_settings(text,value))return value;}
    return value;
}
inline bool save_image_cache_settings(const std::string& path,const ImageCacheSettings& value){
    if(path.empty())return false;char text[80];std::snprintf(text,sizeof(text),"1 %u %u %u\n",value.luaCount,value.readyMib,value.idleMib);
    ImageCacheSettings check;if(!parse_image_cache_settings(text,check))return false;
    const auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fputs(text,f)>=0;if(std::fclose(f)!=0)ok=false;if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}else if(errno!=ENOENT){std::remove(tmp.c_str());return false;}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
