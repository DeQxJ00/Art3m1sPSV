#include "../../host-direct/src/cache_hud_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    namespace fs=std::filesystem;
    auto dir=fs::temp_directory_path()/"art3-cache-hud-settings-test";
    fs::create_directories(dir);auto path=(dir/"cache-hud.txt").string();
    fs::remove(path);fs::remove(path+".bak");
    bool enabled=true;assert(direct::load_cache_hud(path,enabled)&&!enabled);
    assert(direct::save_cache_hud(path,true));assert(direct::load_cache_hud(path,enabled)&&enabled);
    fs::rename(path,path+".bak");assert(direct::load_cache_hud(path,enabled)&&enabled);
    assert(direct::save_cache_hud(path,false));assert(direct::load_cache_hud(path,enabled)&&!enabled);
    for(auto text:{"1 2","2 1","1","1 1 extra",""}){
        {std::ofstream f(path);f<<text;}enabled=true;
        assert(!direct::load_cache_hud(path,enabled)&&!enabled);
    }
    fs::remove(path);fs::remove(path+".bak");fs::remove(dir);
}
