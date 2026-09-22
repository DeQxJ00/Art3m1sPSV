#include "../../host-direct/src/debug_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    namespace fs=std::filesystem;
    auto dir=fs::temp_directory_path()/"art3-debug-settings-test";
    fs::create_directories(dir);auto path=(dir/"debug-settings.txt").string();
    fs::remove(path);fs::remove(path+".bak");
    bool enabled=true;assert(direct::load_debug_settings(path,enabled)&&!enabled);
    assert(direct::save_debug_settings(path,true));assert(direct::load_debug_settings(path,enabled)&&enabled);
    fs::rename(path,path+".bak");assert(direct::load_debug_settings(path,enabled)&&enabled);
    assert(direct::save_debug_settings(path,false));assert(direct::load_debug_settings(path,enabled)&&!enabled);
    for(auto text:{"1 2","2 1","1","1 1 extra",""}){
        {std::ofstream f(path);f<<text;}enabled=true;
        assert(!direct::load_debug_settings(path,enabled)&&!enabled);
    }
    fs::remove(path);fs::remove(path+".bak");fs::remove(dir);
}
