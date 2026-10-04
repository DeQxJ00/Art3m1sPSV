#include "../../host-direct/src/ogv_cache_menu.hpp"
#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    using namespace direct;OgvCacheSettings value;
    assert(value.enabled&&value.groups==4&&value.mib==16);
    for(auto bad:{"1 2 4 16","1 1 0 16","1 1 17 16","1 1 4 3","1 1 4 65","2 1 4 16","1 1 4 16 x"})assert(!parse_ogv_cache_settings(bad,value));
    assert(parse_ogv_cache_settings("1 0 8 32\n",value));assert(!value.enabled&&value.groups==8&&value.mib==32);
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-ogv-cache-settings-test";std::filesystem::create_directories(dir);
    auto path=ogv_cache_settings_path(dir.string(),"../sample");assert(std::filesystem::path(path).parent_path()==dir);
    assert(save_ogv_cache_settings(path,value));assert(load_ogv_cache_settings(path).groups==8);
    assert(load_ogv_cache_settings(ogv_cache_settings_path(dir.string(),".._sample")).groups==4);
    std::filesystem::rename(path,path+".bak");{std::ofstream f(path);f<<"broken";}assert(load_ogv_cache_settings(path).groups==8);
    assert(save_ogv_cache_settings(path,{}));assert(!std::filesystem::exists(path+".bak"));assert(load_ogv_cache_settings(path).groups==4);
    assert(!save_ogv_cache_settings(ogv_cache_settings_path((dir/"missing").string(),"sample"),{}));
    std::filesystem::remove(path);std::filesystem::remove(dir);
    OgvCacheMenu m;SceTouchData t{};m.row=1;m.input(SCE_CTRL_LEFT,false,t);assert(m.value.groups==3);
    for(int i=0;i<30;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.groups==1);
    for(int i=0;i<30;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.groups==16);
    m.row=2;for(int i=0;i<30;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.mib==64);
    m.row=0;m.input(SCE_CTRL_CIRCLE,false,t);assert(!m.value.enabled);
    m.row=1;m.input(SCE_CTRL_LEFT,false,t);assert(m.value.groups==16);
    m.row=3;m.input(SCE_CTRL_CIRCLE,false,t);assert(m.value.enabled&&m.value.groups==4&&m.value.mib==16);
    t.reportNum=1;t.report[0].x=700*2;t.report[0].y=170*2;m.input(0,true,t);assert(m.row==1&&m.value.groups==5);
    m.row=4;assert(m.input(SCE_CTRL_CIRCLE,false,t)==1);assert(m.input(SCE_CTRL_CROSS,false,t)==-1);
    GameSettingsMenu g;g.row=5;assert(g.input(SCE_CTRL_CIRCLE,false,t)==9);g.row=6;assert(g.input(SCE_CTRL_CIRCLE,false,t)==-1);
    assert(GameSettingsMenu::rowsTop+7*GameSettingsMenu::rowPitch<=GameSettingsMenu::overviewTop);
}
