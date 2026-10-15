#include "../../host-direct/src/psb_cache_menu.hpp"
#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    using namespace direct;PsbCacheSettings value;
    assert(value.groups==4&&value.mib==32);
    for(auto bad:{"1 0 32","1 17 32","1 4 7","1 4 129","2 4 32","1 4 32 x"})assert(!parse_psb_cache_settings(bad,value));
    assert(parse_psb_cache_settings("1 8 64\n",value));assert(value.groups==8&&value.mib==64);
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-psb-cache-settings-test";std::filesystem::create_directories(dir);
    auto path=psb_cache_settings_path(dir.string(),"../sample");assert(std::filesystem::path(path).parent_path()==dir);
    assert(save_psb_cache_settings(path,value));assert(load_psb_cache_settings(path).groups==8);
    assert(load_psb_cache_settings(psb_cache_settings_path(dir.string(),".._sample")).groups==4);
    std::filesystem::rename(path,path+".bak");{std::ofstream f(path);f<<"broken";}assert(load_psb_cache_settings(path).groups==8);
    assert(save_psb_cache_settings(path,{}));assert(!std::filesystem::exists(path+".bak"));assert(load_psb_cache_settings(path).groups==4);
    assert(!save_psb_cache_settings(psb_cache_settings_path((dir/"missing").string(),"sample"),{}));
    std::filesystem::remove(path);std::filesystem::remove(dir);
    PsbCacheMenu m;SceTouchData t{};
    for(int i=0;i<30;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.groups==1);
    for(int i=0;i<30;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.groups==16);
    m.row=1;for(int i=0;i<30;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.mib==128);
    for(int i=0;i<30;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.mib==8);
    m.row=2;m.input(SCE_CTRL_CIRCLE,false,t);assert(m.value.groups==4&&m.value.mib==32);
    t.reportNum=1;t.report[0].x=700*2;t.report[0].y=120*2;m.input(0,true,t);assert(m.row==0&&m.value.groups==5);
    m.row=3;assert(m.input(SCE_CTRL_CIRCLE,false,t)==1);assert(m.input(SCE_CTRL_CROSS,false,t)==-1);
    GameSettingsMenu g;g.row=8;assert(g.input(SCE_CTRL_CIRCLE,false,t)==12);g.row=9;assert(g.input(SCE_CTRL_CIRCLE,false,t)==-1);
    assert(GameSettingsMenu::rowsTop+GameSettingsMenu::rowCount*GameSettingsMenu::rowPitch<=GameSettingsMenu::overviewTop);
}
