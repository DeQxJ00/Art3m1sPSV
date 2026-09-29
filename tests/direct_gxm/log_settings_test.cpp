#include "../../host-direct/src/log_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    namespace fs=std::filesystem;
    auto dir=fs::temp_directory_path()/"art3-log-settings-test";
    fs::create_directories(dir);auto path=(dir/"log-settings.txt").string();
    fs::remove(path);fs::remove(path+".bak");
    bool enabled=false;assert(direct::load_log_settings(path,enabled)&&enabled);
    assert(direct::save_log_settings(path,false));assert(direct::load_log_settings(path,enabled)&&!enabled);
    fs::rename(path,path+".bak");assert(direct::load_log_settings(path,enabled)&&!enabled);
    assert(direct::save_log_settings(path,true));assert(direct::load_log_settings(path,enabled)&&enabled);
    for(auto text:{"1 2","2 1","1","1 1 extra",""}){
        {std::ofstream f(path);f<<text;}enabled=false;
        assert(!direct::load_log_settings(path,enabled)&&enabled);
    }
    fs::remove(path);fs::remove(path+".bak");fs::remove(dir);
}
