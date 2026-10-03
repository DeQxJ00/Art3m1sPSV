#include "cpu_cache_menu.hpp"
#include <dirent.h>
#include <psp2/io/stat.h>
#include <set>
extern "C"{void* pfs_open_single(const char*,const char*);void pfs_close(void*);int pfs_entry_count(void*);int pfs_entry_path(void*,int,char*,int);}
namespace direct {
namespace {
void add_path(std::set<std::string>& folders,const std::string& path,bool& failed){
    auto p=cpu_cache_folder(path);if(p.size()<4||p.substr(p.size()-4)!=".png")return;
    auto slash=p.rfind('/');if(slash==p.npos)return;auto dir=p.substr(0,slash);
    // Present asset families (image/bg, image/fg, etc.), including platform prefixes.
    auto image=dir.rfind("/image/");size_t start=dir.rfind("image/",0)==0?6:image!=dir.npos?image+7:0;
    if(start){auto end=dir.find('/',start);if(end!=dir.npos)dir.resize(end);}
    if(folders.size()>=128&&!folders.count(dir)){failed=true;return;}folders.insert(dir);
}
void loose(const std::string& root,const std::string& relative,unsigned depth,std::set<std::string>& folders,bool& failed){
    if(depth>12){failed=true;return;}auto full=root+(relative.empty()?"":"/"+relative);DIR* d=opendir(full.c_str());if(!d){failed=true;return;}
    while(auto* e=readdir(d)){std::string name=e->d_name;if(name=="."||name=="..")continue;std::string path=relative.empty()?name:relative+"/"+name;
        SceIoStat s{};if(sceIoGetstat((root+"/"+path).c_str(),&s)<0){failed=true;continue;}
        if(SCE_S_ISDIR(s.st_mode)){if(name.find(".pfs")!=name.npos||name=="shader-cache"||name=="save")continue;loose(root,path,depth+1,folders,failed);}else add_path(folders,path,failed);
    }closedir(d);
}
}
std::vector<std::string> cpu_cache_scan_folders(const std::string& root,bool& failed){
    failed=false;std::set<std::string> folders;loose(root,"",0,folders,failed);
    DIR* d=opendir(root.c_str());if(!d){failed=true;return {};}
    std::vector<std::string> names;
    while(auto* e=readdir(d)){std::string name=e->d_name;auto at=name.find(".pfs");if(at==name.npos)continue;auto suffix=name.substr(at+4);
        if(!suffix.empty()&&(suffix.size()!=4||suffix[0]!='.'||suffix.find_first_not_of("0123456789",1)!=suffix.npos))continue;
        names.push_back(name);
    }closedir(d);std::sort(names.begin(),names.end());
    for(const auto& name:names){void* p=pfs_open_single((root+"/"+name).c_str(),"auto");if(!p){failed=true;continue;}
        const int n=pfs_entry_count(p);if(n<0)failed=true;
        for(int i=0;i<n;++i){char path[1024];int len=pfs_entry_path(p,i,path,sizeof(path));if(len<0||len>=int(sizeof(path))-1){failed=true;continue;}add_path(folders,path,failed);}pfs_close(p);
    }
    return {folders.begin(),folders.end()};
}
}
