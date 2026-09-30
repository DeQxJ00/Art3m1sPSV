#include "../../host-direct/src/background_alpha_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

int main(){
    using namespace direct;
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-bg-alpha-test";
    std::filesystem::create_directories(dir);
    const auto path=background_alpha_settings_path(dir.string(),"../sample");
    assert(std::filesystem::path(path).parent_path()==dir);
    assert(load_background_alpha_settings(dir.string(),"../sample"));
    assert(save_background_alpha_settings(dir.string(),"../sample",false));
    assert(!load_background_alpha_settings(dir.string(),"../sample"));
    assert(load_background_alpha_settings(dir.string(),".._sample"));
    std::filesystem::rename(path,path+".bak");
    assert(!load_background_alpha_settings(dir.string(),"../sample"));
    assert(save_background_alpha_settings(dir.string(),"../sample",true));
    assert(load_background_alpha_settings(dir.string(),"../sample"));
    assert(!std::filesystem::exists(path+".bak"));
    for(auto invalid:{"broken","2 1","1 2","1 1 extra"}){
        {std::ofstream file(path);file<<invalid;}
        assert(load_background_alpha_settings(dir.string(),"../sample"));
    }
    assert(!save_background_alpha_settings(dir.string(),"",false));
    assert(!save_background_alpha_settings((dir/"missing").string(),"sample",false));
    std::filesystem::remove(path);std::filesystem::remove(dir);
}
