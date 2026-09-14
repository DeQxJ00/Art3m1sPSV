#pragma once
#include <cstdio>
#include <cerrno>
#include <string>

namespace direct {
struct ShaderSettings { bool convert=false, compile=false; };
inline bool parse_shader_settings(const char* text,ShaderSettings& out){
    int version,a,b;char extra;
    if(std::sscanf(text,"%d %d %d %c",&version,&a,&b,&extra)!=3||version!=1||(a!=0&&a!=1)||(b!=0&&b!=1))return false;
    out={a!=0,b!=0};return true;
}
inline bool load_shader_settings(const std::string& path,ShaderSettings& out){
    out={};FILE* f=std::fopen(path.c_str(),"rb");
    if(!f){if(errno!=ENOENT)return false;f=std::fopen((path+".bak").c_str(),"rb");if(!f)return errno==ENOENT;}
    char text[64]{};auto n=std::fread(text,1,sizeof(text)-1,f);bool ok=!std::ferror(f)&&n<sizeof(text)-1;
    std::fclose(f);return ok&&parse_shader_settings(text,out);
}
inline bool save_shader_settings(const std::string& path,const ShaderSettings& value){
    const auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fprintf(f,"1 %d %d\n",int(value.convert),int(value.compile))>0;
    ok=std::fclose(f)==0&&ok;if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
