#pragma once
#include "cpu_cache_settings.hpp"
namespace direct {
struct LargeImageScope { bool automatic=true,pregenerate=true;std::vector<std::string> folders{"image/bg"}; };
inline bool large_image_scope_matches(const LargeImageScope& scope,const std::string& name){
    const auto path=cpu_cache_folder(name);if(path.empty())return false;
    if(scope.automatic)return true;
    for(const auto& f:scope.folders)if(path.size()>f.size()&&path.compare(0,f.size(),f)==0&&path[f.size()]=='/')return true;
    return false;
}
inline std::string large_image_scope_path(const std::string& dir,const std::string& id){auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".large-image-scope");return p;}
inline bool parse_large_image_scope(const std::string& text,LargeImageScope& out){
    if(text.size()>32768)return false;
    std::istringstream in(text);std::string line;unsigned version,automatic;char tail;
    if(!std::getline(in,line)||std::sscanf(line.c_str(),"%u %u %c",&version,&automatic,&tail)!=2||(version!=1&&version!=2)||automatic>(version==1?1u:2u))return false;
    LargeImageScope value;value.automatic=version==1?automatic!=0:automatic==0;value.pregenerate=version==1||automatic==2;value.folders.clear();
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto f=cpu_cache_folder(line);if(f.empty()||value.folders.size()>=128)return false;
        if(std::find(value.folders.begin(),value.folders.end(),f)==value.folders.end())value.folders.push_back(f);
    }
    out=std::move(value);return true;
}
inline LargeImageScope load_large_image_scope(const std::string& path){
    LargeImageScope out;if(path.empty())return out;FILE* f=std::fopen(path.c_str(),"rb");if(!f)f=std::fopen((path+".bak").c_str(),"rb");if(!f)return out;
    std::string text(32769,'\0');size_t n=std::fread(text.data(),1,text.size(),f);bool ok=!std::ferror(f);std::fclose(f);text.resize(n);if(ok)parse_large_image_scope(text,out);return out;
}
inline bool save_large_image_scope(const std::string& path,const LargeImageScope& value){
    if(path.empty())return false;std::string text="2 "+std::to_string(value.automatic?0u:value.pregenerate?2u:1u)+"\n";
    for(const auto& f:value.folders)text+=f+"\n";LargeImageScope check;if(!parse_large_image_scope(text,check))return false;
    auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();ok=std::fclose(f)==0&&ok;
    if(!ok){std::remove(tmp.c_str());return false;}
    bool existed=false;if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}else if(errno!=ENOENT){std::remove(tmp.c_str());return false;}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
