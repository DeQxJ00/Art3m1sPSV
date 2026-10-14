#pragma once
#include "font_settings.hpp"
#include <cerrno>

namespace direct {
inline bool valid_emote_backend(unsigned value) {
    return value==0||value==2;
}
#ifdef DIRECT_RECOVERED_PACKET
constexpr bool kRecoveredEmoteAvailable=true;
#else
constexpr bool kRecoveredEmoteAvailable=false;
#endif
constexpr unsigned kDefaultEmoteBackend = kRecoveredEmoteAvailable ? 2 : 0;
inline unsigned effective_emote_backend(unsigned saved) {
    return kRecoveredEmoteAvailable&&saved==2?2:0;
}
inline unsigned effective_emote_mesh(unsigned backend,unsigned savedMesh) {
    return effective_emote_backend(backend)==2?100:savedMesh;
}
inline const char* emote_backend_label(unsigned value) {
    return effective_emote_backend(value)==2?"新emote引擎（默认）":"旧emote引擎";
}
inline std::string emote_backend_settings_path(const std::string& directory,const std::string& id) {
    auto path=font_settings_path(directory,id);
    if(!path.empty())path.replace(path.size()-5,5,".emote-backend");
    return path;
}
inline unsigned load_emote_backend_settings(const std::string& directory,const std::string& id) {
    const auto path=emote_backend_settings_path(directory,id);
    if(path.empty())return kDefaultEmoteBackend;
    FILE* f=std::fopen(path.c_str(),"rb");
    if(!f)f=std::fopen((path+".bak").c_str(),"rb");
    if(!f)return kDefaultEmoteBackend;
    char text[64]{};size_t n=std::fread(text,1,sizeof(text)-1,f);
    bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
    unsigned version=0,value=0;char extra;
    return ok&&std::sscanf(text,"%u %u %c",&version,&value,&extra)==2&&version==1&&valid_emote_backend(value)?value:kDefaultEmoteBackend;
}
inline bool save_emote_backend_settings(const std::string& directory,const std::string& id,unsigned value) {
    const auto path=emote_backend_settings_path(directory,id);
    if(path.empty()||!valid_emote_backend(value))return false;
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
