#include <filesystem>
#include <fstream>
#include <cstring>
#include <cassert>
#include <psp2/io/stat.h>
extern "C" int sceIoGetstat(const char* path,SceIoStat* out){
    std::error_code error;auto status=std::filesystem::status(path,error);if(error)return -1;
    out->st_mode=std::filesystem::is_directory(status)?SCE_S_IFDIR:SCE_S_IFREG;return 0;
}
extern "C" void* pfs_open_single(const char*,const char*){return reinterpret_cast<void*>(1);}
extern "C" void pfs_close(void*){}
extern "C" int pfs_entry_count(void*){return 3;}
extern "C" int pfs_entry_path(void*,int i,char* out,int){
    const char* paths[]={"image/bg/loose.JPG","image/ev/archive.jpeg","image/fg/person.png"};
    std::strcpy(out,paths[i]);return std::strlen(out);
}
#include "../../host-direct/src/cpu_cache_menu.cpp"
int main(){
    auto root=std::filesystem::temp_directory_path()/"art3-image-scan-test";
    std::filesystem::create_directories(root/"image/bg");std::filesystem::create_directories(root/"texture-cache");
    for(auto p:{"image/bg/loose.JPG","texture-cache/ignored.png","assets.pfs"})std::ofstream(root/p).put('x');
    bool error=false;auto files=direct::large_image_scan_files(root.string(),error);
    assert(!error&&files.size()==3);
    auto folders=direct::large_image_scan_folders(root.string(),error);
    assert(!error&&folders==std::vector<std::string>({"image/bg","image/ev","image/fg"}));
    auto png=direct::cpu_cache_scan_folders(root.string(),error);assert(png==std::vector<std::string>({"image/fg"}));
    std::filesystem::remove_all(root);
}
