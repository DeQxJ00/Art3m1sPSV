#pragma once
#include "debug_settings.hpp"
#include "font_settings.hpp"
namespace direct {
inline std::string large_image_settings_path(const std::string& dir,const std::string& id){
    auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".large-image-bc3");return p;
}
inline bool load_large_image_settings(const std::string& dir,const std::string& id){
    bool enabled=true;load_debug_settings(large_image_settings_path(dir,id),enabled,true);return enabled;
}
inline bool save_large_image_settings(const std::string& dir,const std::string& id,bool enabled){
    auto p=large_image_settings_path(dir,id);return !p.empty()&&save_debug_settings(p,enabled);
}
inline bool valid_large_image_threshold(unsigned v){return v>=1&&v<=64;}
inline std::string large_image_threshold_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".large-image-threshold");
    return path;
}
inline unsigned load_large_image_threshold_settings(const std::string& directory,const std::string& id) {
    const auto path=large_image_threshold_settings_path(directory,id);
    if(path.empty())return 30;
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(!f)return 30;
    char text[64]{};size_t n=std::fread(text,1,sizeof(text)-1,f);
    bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
    unsigned version=0,value=0;char extra;
    return ok&&std::sscanf(text,"%u %u %c",&version,&value,&extra)==2&&version==1&&valid_large_image_threshold(value)?value:30;
}
inline bool save_large_image_threshold_settings(const std::string& directory,const std::string& id,unsigned value) {
    const auto path=large_image_threshold_settings_path(directory,id);
    if(path.empty()||!valid_large_image_threshold(value))return false;
    const auto tmp=path+".tmp",bak=path+".bak";
    FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"1 %u\n",value)>0;ok=std::fclose(f)==0&&ok;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;
    if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    else if(errno!=ENOENT){std::remove(tmp.c_str());return false;}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
