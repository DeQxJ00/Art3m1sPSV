#pragma once
#include "font_settings.hpp"
#include <cerrno>

namespace direct {
constexpr unsigned kDefaultEmoteMesh = 40;
inline bool valid_emote_mesh(unsigned value) {
    return value==100||value==80||value==60||value==40;
}
inline unsigned step_emote_mesh(unsigned value,bool backwards) {
    const unsigned choices[]={100,80,60,40};
    for(unsigned i=0;i<4;i++)if(choices[i]==value)return choices[(i+(backwards?3:1))%4];
    return kDefaultEmoteMesh;
}
inline const char* emote_mesh_label(unsigned value) {
    switch(value){case 100:return "1.0";case 80:return "0.8";case 60:return "0.6";default:return "0.4（默认）";}
}
inline std::string emote_mesh_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".emote-mesh");
    return path;
}
inline unsigned load_emote_mesh_settings(const std::string& directory,const std::string& id) {
    const auto path=emote_mesh_settings_path(directory,id);
    if(path.empty())return kDefaultEmoteMesh;
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(!f)return kDefaultEmoteMesh;
    char text[64]{};size_t n=std::fread(text,1,sizeof(text)-1,f);
    bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
    unsigned version=0,value=0;char extra;
    return ok&&std::sscanf(text,"%u %u %c",&version,&value,&extra)==2&&version==1&&valid_emote_mesh(value)?value:kDefaultEmoteMesh;
}
inline bool save_emote_mesh_settings(const std::string& directory,const std::string& id,unsigned value) {
    const auto path=emote_mesh_settings_path(directory,id);
    if(path.empty()||!valid_emote_mesh(value))return false;
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
