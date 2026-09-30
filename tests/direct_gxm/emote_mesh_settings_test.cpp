#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
    using namespace direct;
    auto dir=std::filesystem::temp_directory_path()/"art3m1s-emote-mesh-settings-test";
    std::filesystem::create_directories(dir);
    const auto directory=dir.string();
    const auto path=emote_mesh_settings_path(directory,"../model");
    assert(std::filesystem::path(path).parent_path()==dir);
    assert(path!=emote_mesh_settings_path(directory,".._model"));
    assert(load_emote_mesh_settings(directory,"../model")==100);
    unsigned value=40;
    for(unsigned expected:{100u,80u,60u,40u}){
        value=step_emote_mesh(value,false);assert(value==expected);
        assert(save_emote_mesh_settings(directory,"../model",value));
        assert(load_emote_mesh_settings(directory,"../model")==value);
        assert(load_emote_mesh_settings(directory,".._model")==100);
    }
    for(unsigned invalid:{0u,20u,25u,41u,101u}){
        assert(!save_emote_mesh_settings(directory,"../model",invalid));
        assert(load_emote_mesh_settings(directory,"../model")==40);
    }
    assert(save_emote_mesh_settings(directory,"../model",80));
    std::filesystem::rename(path,path+".bak");
    assert(load_emote_mesh_settings(directory,"../model")==80);
    assert(save_emote_mesh_settings(directory,"../model",60));
    assert(!std::filesystem::exists(path+".bak"));
    for(auto text:{"1 25","1 20","2 40","1 60 extra","broken"}){
        {std::ofstream f(path);f<<text;}
        assert(load_emote_mesh_settings(directory,"../model")==100);
    }
    assert(!save_emote_mesh_settings(directory,"",40));
    std::filesystem::remove(path);std::filesystem::remove(dir);

    GameSettingsMenu menu;SceTouchData touch{};
    assert(menu.emoteMesh==100);
    assert(menu.platform==0);
    menu.row=1;
    assert(menu.input(SCE_CTRL_RIGHT,false,touch)==2);
    assert(menu.input(SCE_CTRL_LEFT,false,touch)==6);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==2);
    touch.reportNum=1;touch.report[0].x=650*2;touch.report[0].y=210*2;
    assert(menu.input(0,true,touch)==2);
    menu.row=0;
    for(int i=0;i<2;i++)assert(menu.input(SCE_CTRL_DOWN,false,touch)==0);
    assert(menu.row==2&&menu.input(SCE_CTRL_RIGHT,false,touch)==4);
    assert(menu.input(SCE_CTRL_LEFT,false,touch)==5);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==4);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==3);
    assert(!menu.ignoreBackgroundAlpha&&menu.input(SCE_CTRL_CIRCLE,false,touch)==7);
    assert(menu.input(SCE_CTRL_LEFT,false,touch)==7);
    assert(menu.input(SCE_CTRL_RIGHT,false,touch)==7);
    touch.report[0].y=280*2;assert(menu.input(0,true,touch)==7);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==4);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==-1);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==0);
    touch.reportNum=1;touch.report[0].x=700*2;touch.report[0].y=232*2;
    assert(menu.input(0,true,touch)==4&&menu.row==2);
    assert(menu.input(SCE_CTRL_CROSS,false,touch)==-1);
    std::cout<<"PASS: mesh defaults, four choices, persistence, isolation, recovery and menu controls\n";
}
