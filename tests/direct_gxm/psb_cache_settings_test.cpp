#include "../../host-direct/src/psb_cache_menu.hpp"
#include "../../host-direct/src/game_settings_menu.hpp"
#include "../../host-direct/src/psb_cache_auto.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    using namespace direct;PsbCacheSettings value;
    assert(value.groups==4&&value.mib==32&&value.automatic);
    for(auto bad:{"1 0 32","1 17 32","1 4 0","2 4 4096 0 0","2 4 32 2 0","2 4 32 1 31","2 4 32","1 4 32 x"})assert(!parse_psb_cache_settings(bad,value));
    assert(parse_psb_cache_settings("1 8 64\n",value));assert(value.groups==8&&value.mib==64&&!value.automatic);
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
    m.row=2;for(int i=0;i<30;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.mib==272&&!m.value.automatic);
    for(int i=0;i<30;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.mib==32);
    m.row=3;m.input(SCE_CTRL_CIRCLE,false,t);assert(m.value.groups==4&&m.value.mib==32);
    t.reportNum=1;t.report[0].x=700*2;t.report[0].y=120*2;m.input(0,true,t);assert(m.row==0&&m.value.groups==5);
    m.row=4;assert(m.input(SCE_CTRL_CIRCLE,false,t)==1);assert(m.input(SCE_CTRL_CROSS,false,t)==-1);
    assert(parse_psb_cache_settings("2 4 256 0 64",value)&&value.capacity()==256);
    assert(parse_psb_cache_settings("2 4 256 1 64",value)&&value.capacity()==64);
    constexpr uint64_t M=1048576;
    assert(estimate_psb_cache({}).mib==32);
    assert(estimate_psb_cache({8*M}).mib==34);
    assert(estimate_psb_cache({8*M,12*M}).mib==42);
    auto e=estimate_psb_cache({M,8*M,10*M,12*M,100*M});
    assert(e.mib==42&&e.trimmed==10*M&&e.median==10*M);
    e=estimate_psb_cache({M,2*M,12*M,12*M,100*M});
    assert(e.mib==50&&e.median>e.trimmed);
    e=estimate_psb_cache({M,8*M,8*M,14*M,100*M});
    assert(e.mib==42&&e.trimmed>e.median);
    assert(estimate_psb_cache({8*M+1}).mib==35);
    assert(estimate_psb_cache({1,1,1}).mib==32);
    assert(estimate_psb_cache({UINT64_MAX,UINT64_MAX}).mib==64);
    PsbCacheInventory inv;PsbCacheInventory::collect("a\\b.psb",M,&inv);
    PsbCacheInventory::collect("a/b.psb",2*M,&inv);
    assert(inv.files.size()==1&&inv.files.begin()->second==2*M);
    GameSettingsMenu g;g.row=8;assert(g.input(SCE_CTRL_CIRCLE,false,t)==12);g.row=GameSettingsMenu::rowCount-1;assert(g.input(SCE_CTRL_CIRCLE,false,t)==-1);
    assert(GameSettingsMenu::rowsTop+GameSettingsMenu::visibleRows*GameSettingsMenu::rowPitch<=GameSettingsMenu::overviewTop);
}
