#include "../../host-direct/src/cpu_cache_menu.hpp"
#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    using namespace direct;CpuCacheSettings value;
    assert(value.sizeCheck&&value.minKiB==512);
    assert(!value.enabled&&value.ratio&&!value.runs&&value.percent==60&&value.mean==256&&value.folders.empty());
    assert(parse_cpu_cache_settings("1 1 1 512 0 65 1 512\nIMAGE\\FG\nimage/bg\n",value));
    assert(value.enabled&&!value.ratio&&value.runs&&value.mean==512&&value.folders[0]=="image/fg");
    for(auto bad:{"1 2 1 512 1 60 0 256\n","1 1 1 512 1 101 0 256\n","1 1 1 512 1 60 0 63\n","1 1 1 512 1 60 0 256 extra\n","1 1 1 512 1 60 0 256\n../fg\n","1 1 1 512 1 60 0 256\nimage//fg\n"})assert(!parse_cpu_cache_settings(bad,value));
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-cpu-cache-settings-test";
    std::filesystem::create_directories(dir);auto path=cpu_cache_settings_path(dir.string(),"../sample");
    assert(std::filesystem::path(path).parent_path()==dir);assert(save_cpu_cache_settings(path,value));
    auto loaded=load_cpu_cache_settings(path);assert(loaded.enabled&&loaded.folders==value.folders&&loaded.runs&&!loaded.ratio);
    assert(!load_cpu_cache_settings(cpu_cache_settings_path(dir.string(),".._sample")).enabled);
    std::filesystem::rename(path,path+".bak");assert(load_cpu_cache_settings(path).enabled);
    assert(save_cpu_cache_settings(path,value));assert(!std::filesystem::exists(path+".bak"));
    assert(!save_cpu_cache_settings(cpu_cache_settings_path((dir/"missing").string(),"sample"),value));
    std::filesystem::remove(path);std::filesystem::remove(dir);
    SceTouchData touch{};CpuCacheMenu m;m.scanned({"image/bg","image/fg"},false);
    assert(m.input(SCE_CTRL_CIRCLE,false,touch)==0&&m.value.enabled);
    m.row=CpuCacheMenu::Minimum;m.input(SCE_CTRL_LEFT,false,touch);assert(m.value.minKiB==256);
    m.row=CpuCacheMenu::SizeCheck;m.input(SCE_CTRL_CIRCLE,false,touch);assert(!m.value.sizeCheck);
    m.row=CpuCacheMenu::RatioCheck;m.input(SCE_CTRL_CIRCLE,false,touch);assert(!m.value.ratio&&!m.value.runs);
    m.row=CpuCacheMenu::RunCheck;m.input(SCE_CTRL_CIRCLE,false,touch);assert(m.value.runs&&!m.value.ratio);
    m.row=CpuCacheMenu::Mean;m.input(SCE_CTRL_LEFT,false,touch);assert(m.value.mean==128);
    m.row=CpuCacheMenu::FirstFolder+1;m.input(SCE_CTRL_CIRCLE,false,touch);assert(m.value.folders==std::vector<std::string>{"image/fg"});
    m.row=m.count()-2;assert(m.input(SCE_CTRL_CIRCLE,false,touch)==1);m.row=m.count()-1;assert(m.input(SCE_CTRL_CIRCLE,false,touch)==-1);
    m.row=0;m.input(SCE_CTRL_CIRCLE,false,touch);m.row=CpuCacheMenu::RunCheck;m.input(SCE_CTRL_CIRCLE,false,touch);assert(!m.value.enabled&&m.value.runs);
    m.row=0;m.input(SCE_CTRL_UP,false,touch);assert(m.row==m.count()-1);m.input(SCE_CTRL_DOWN,false,touch);assert(m.row==0);
    GameSettingsMenu g;g.row=4;assert(g.input(SCE_CTRL_CIRCLE,false,touch)==8);g.row=5;assert(g.input(SCE_CTRL_CIRCLE,false,touch)==9);g.row=6;assert(g.input(SCE_CTRL_CIRCLE,false,touch)==-1);
    touch.reportNum=1;touch.report[0].x=200*2;touch.report[0].y=270*2;assert(g.input(0,true,touch)==8);
}
