#pragma once
#include <cstdio>
#include <cerrno>
#include <string>
namespace direct {
inline bool load_debug_settings(const std::string& path,bool& enabled){
    enabled=false;FILE* f=std::fopen(path.c_str(),"rb");
    if(!f){if(errno!=ENOENT)return false;f=std::fopen((path+".bak").c_str(),"rb");if(!f)return errno==ENOENT;}
    char text[32]{};size_t n=std::fread(text,1,sizeof(text)-1,f);bool ok=!std::ferror(f)&&n<sizeof(text)-1;std::fclose(f);
    int version=0,value=0;char tail;
    ok=ok&&std::sscanf(text,"%d %d %c",&version,&value,&tail)==2&&version==1&&(value==0||value==1);
    if(ok)enabled=value!=0;return ok;
}
inline bool save_debug_settings(const std::string& path,bool enabled){
    auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"1 %d\n",int(enabled))>0;ok=std::fclose(f)==0&&ok;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
