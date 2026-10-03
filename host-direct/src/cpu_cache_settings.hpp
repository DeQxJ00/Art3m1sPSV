#pragma once
#include "font_settings.hpp"
#include <algorithm>
#include <sstream>
#include <vector>

namespace direct {
struct CpuCacheSettings {
    bool enabled=false,sizeCheck=true,ratio=true,runs=false;
    unsigned minKiB=512,percent=60,mean=256;
    std::vector<std::string> folders;
};
inline std::string cpu_cache_folder(std::string p){
    for(char& c:p){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
    while(!p.empty()&&p.back()=='/')p.pop_back();
    if(p.empty()||p.size()>240||p.front()=='/'||p.find(':')!=p.npos||p.find('|')!=p.npos)return {};
    std::istringstream in(p);std::string part;
    while(std::getline(in,part,'/'))if(part.empty()||part=="."||part=="..")return {};
    for(unsigned char c:p)if(c<32||c==127)return {};
    return p;
}
inline bool parse_cpu_cache_settings(const std::string& text,CpuCacheSettings& out){
    if(text.size()>32768)return false;
    std::istringstream in(text);std::string line;unsigned v,on,size,minKiB,ratio,percent,runs,mean;char extra;
    if(!std::getline(in,line)||std::sscanf(line.c_str(),"%u %u %u %u %u %u %u %u %c",&v,&on,&size,&minKiB,&ratio,&percent,&runs,&mean,&extra)!=8
        ||v!=1||on>1||size>1||ratio>1||runs>1||minKiB<128||minKiB>16384||percent>100||mean<64||mean>65536)return false;
    CpuCacheSettings result;result.enabled=on;result.sizeCheck=size;result.minKiB=minKiB;result.ratio=ratio;result.percent=percent;result.runs=runs;result.mean=mean;
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        auto folder=cpu_cache_folder(line);if(folder.empty()||result.folders.size()>=128)return false;
        if(std::find(result.folders.begin(),result.folders.end(),folder)==result.folders.end())result.folders.push_back(folder);
    }
    out=std::move(result);return true;
}
inline std::string cpu_cache_settings_path(const std::string& dir,const std::string& id){auto p=font_settings_path(dir,id);if(!p.empty())p.replace(p.size()-5,5,".cpu-cache");return p;}
inline std::string cpu_cache_folders(const CpuCacheSettings& v){std::string out;for(const auto& f:v.folders){out+=f;out+='\n';}return out;}
inline CpuCacheSettings load_cpu_cache_settings(const std::string& path){
    CpuCacheSettings out;if(path.empty())return out;FILE* f=std::fopen(path.c_str(),"rb");if(!f)f=std::fopen((path+".bak").c_str(),"rb");if(!f)return out;
    std::string text(32769,'\0');size_t n=std::fread(text.data(),1,text.size(),f);std::fclose(f);text.resize(n);parse_cpu_cache_settings(text,out);return out;
}
inline bool save_cpu_cache_settings(const std::string& path,const CpuCacheSettings& v){
    if(path.empty())return false;char header[96];std::snprintf(header,sizeof(header),"1 %u %u %u %u %u %u %u\n",unsigned(v.enabled),unsigned(v.sizeCheck),v.minKiB,unsigned(v.ratio),v.percent,unsigned(v.runs),v.mean);
    const auto text=std::string(header)+cpu_cache_folders(v);CpuCacheSettings check;if(!parse_cpu_cache_settings(text,check))return false;
    const auto tmp=path+".tmp",bak=path+".bak";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;
    bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size();if(std::fclose(f)!=0)ok=false;
    if(!ok){std::remove(tmp.c_str());return false;}bool existed=false;
    if(FILE* old=std::fopen(path.c_str(),"rb")){existed=true;std::fclose(old);}
    if(existed){std::remove(bak.c_str());if(std::rename(path.c_str(),bak.c_str())!=0){std::remove(tmp.c_str());return false;}}
    if(std::rename(tmp.c_str(),path.c_str())!=0){if(existed)std::rename(bak.c_str(),path.c_str());std::remove(tmp.c_str());return false;}
    std::remove(bak.c_str());return true;
}
}
