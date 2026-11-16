#include "../../host-direct/src/image_cache_menu.hpp"
#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
    using namespace direct;ImageCacheSettings v;
    assert(v.luaCount==128&&v.readyMib==64&&v.idleMib==96);
    for(auto bad:{"", "2 128 64 96", "1 0 64 96", "1 4097 64 96", "1 128 0 96", "1 128 193 96", "1 128 64 193", "1 128 64", "1 128 64 96 x"})assert(!parse_image_cache_settings(bad,v));
    assert(parse_image_cache_settings("1 256 48 80\n",v));
    auto dir=std::filesystem::temp_directory_path()/"art3m1s-image-cache-settings-test";std::filesystem::create_directories(dir);
    auto p=image_cache_settings_path(dir.string(),"../sample01");assert(std::filesystem::path(p).parent_path()==dir);
    assert(save_image_cache_settings(p,v));assert(load_image_cache_settings(p).luaCount==256);
    assert(load_image_cache_settings(image_cache_settings_path(dir.string(),"sample02")).readyMib==64);
    std::filesystem::rename(p,p+".bak");{std::ofstream f(p);f<<"invalid";}
    assert(load_image_cache_settings(p).idleMib==80);
    assert(save_image_cache_settings(p,{}));assert(!std::filesystem::exists(p+".bak"));
    assert(!save_image_cache_settings(image_cache_settings_path((dir/"missing").string(),"sample01"),v));
    std::filesystem::remove(p);std::filesystem::remove(dir);
    ImageCacheMenu m;SceTouchData t{};
    for(int i=0;i<500;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.luaCount==16);
    for(int i=0;i<500;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.luaCount==4096);
    m.row=1;for(int i=0;i<100;++i)m.input(SCE_CTRL_LEFT,false,t);assert(m.value.readyMib==8);
    m.row=2;for(int i=0;i<100;++i)m.input(SCE_CTRL_RIGHT,false,t);assert(m.value.idleMib==192);
    m.row=3;m.input(SCE_CTRL_CIRCLE,false,t);assert(m.value.luaCount==128&&m.value.readyMib==64&&m.value.idleMib==96);
    t.reportNum=1;t.report[0].x=700*2;t.report[0].y=160*2;m.input(0,true,t);assert(m.row==1&&m.value.readyMib==72);
    m.row=4;assert(m.input(SCE_CTRL_CIRCLE,false,t)==1);assert(m.input(SCE_CTRL_CROSS,false,t)==-1);
    uiLanguage=UiLanguage::English;
    for(auto text:ImageCacheMenu::labels)assert(std::strcmp(ui_translate(text),text)!=0);
    for(auto text:ImageCacheMenu::notes)assert(std::strcmp(ui_translate(text),text)!=0);
    assert(m.text(0)=="128 (default)");uiLanguage=UiLanguage::Chinese;
    GameSettingsMenu g;g.row=13;assert(g.input(SCE_CTRL_CIRCLE,false,t)==17);
    g.row=0;g.input(SCE_CTRL_LTRIGGER,false,t);assert(g.row==14);assert(g.input(SCE_CTRL_CIRCLE,false,t)==-1);
    g.input(SCE_CTRL_RTRIGGER,false,t);assert(g.row==0);
}
