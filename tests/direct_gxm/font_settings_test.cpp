#include "../../host-direct/src/font_settings.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
int main(){
    using namespace direct;FontSettings value;
    assert(!value.enabled&&value.name==100&&value.dialogue==100);
    assert(parse_font_settings("1 0 125 145\n",value));
    assert(!value.enabled&&value.name==125&&value.dialogue==145);
    for(auto s:{"1 1 74 100","1 1 100 151","1 2 100 100","2 1 100 100","1 1 100","1 1 100 100 extra"})assert(!parse_font_settings(s,value));
    auto dir=std::filesystem::temp_directory_path()/"art3m1s-font-settings-test";
    std::filesystem::create_directories(dir);
    auto a=font_settings_path(dir.string(),"../game");auto b=font_settings_path(dir.string(),".._game");
    assert(a!=b&&std::filesystem::path(a).parent_path()==dir);
    assert(save_font_settings(a,value));assert(load_font_settings(a).name==125);
    value.enabled=true;assert(save_font_settings(b,value));assert(load_font_settings(b).enabled);
    assert(!load_font_settings(a).enabled);
    value.name=150;assert(save_font_settings(a,value));assert(load_font_settings(a).name==150);
    std::filesystem::rename(a,a+".bak");assert(load_font_settings(a).name==150);
    assert(save_font_settings(a,value));assert(!std::filesystem::exists(a+".bak"));
    {std::ofstream f(a);f<<"broken";}assert(!load_font_settings(a).enabled);
    std::filesystem::remove(a);std::filesystem::remove(b);std::filesystem::remove(dir);
    std::cout<<"font settings defaults, bounds, isolation, persistence and interrupted rename recovery passed\n";
}
