#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){
    using namespace direct;
    const auto dir=std::filesystem::temp_directory_path()/"art3m1s-emote-backend-test";
    std::filesystem::create_directories(dir);
    const auto folder=dir.string(),path=emote_backend_settings_path(folder,"../game");
    assert(std::filesystem::path(path).parent_path()==dir);
    assert(load_emote_backend_settings(folder,"../game")==kDefaultEmoteBackend);
    assert(save_emote_mesh_settings(folder,"../game",60));
    assert(save_emote_backend_settings(folder,"../game",2));
    assert(load_emote_backend_settings(folder,"../game")==2);
    assert(load_emote_backend_settings(folder,".._game")==kDefaultEmoteBackend);
    assert(effective_emote_backend(2)==(kRecoveredEmoteAvailable?2u:0u));
    assert(effective_emote_mesh(2,60)==(kRecoveredEmoteAvailable?100u:60u));
    assert(load_emote_mesh_settings(folder,"../game")==60);
    assert(save_emote_backend_settings(folder,"../game",0));
    assert(effective_emote_mesh(load_emote_backend_settings(folder,"../game"),60)==60);
    assert(!save_emote_backend_settings(folder,"../game",1));
    assert(!save_emote_backend_settings(folder,"",2));
    assert(!save_emote_backend_settings(folder+"/missing","game",2));
    assert(save_emote_backend_settings(folder,"../game",2));
    std::filesystem::rename(path,path+".bak");
    assert(load_emote_backend_settings(folder,"../game")==2);
    assert(save_emote_backend_settings(folder,"../game",0));
    assert(!std::filesystem::exists(path+".bak"));
    for(auto text:{"broken","2 2","1 1","1 2 extra","1 -1"}){
        {std::ofstream f(path);f<<text;}
        assert(load_emote_backend_settings(folder,"../game")==kDefaultEmoteBackend);
    }
    GameSettingsMenu m;SceTouchData t{};
    assert(m.emoteBackend==(kRecoveredEmoteAvailable?2u:0u));
    m.row=7;
    for(auto key:{SCE_CTRL_CIRCLE,SCE_CTRL_LEFT,SCE_CTRL_RIGHT})
        assert(m.input(key,false,t)==(kRecoveredEmoteAvailable?11:0));
    t.reportNum=1;t.report[0].x=700*2;t.report[0].y=(m.rowsTop+12)*2;
    assert(m.input(0,true,t)==(kRecoveredEmoteAvailable?11:0));
    m.emoteBackend=2;m.emoteMesh=60;m.row=2;
    assert(m.input(SCE_CTRL_CIRCLE,false,t)==4);
    assert(m.input(SCE_CTRL_LEFT,false,t)==5);
    m.emoteBackend=0;
    assert(m.input(SCE_CTRL_CIRCLE,false,t)==4&&m.emoteMesh==60);
    m.row=0;m.input(SCE_CTRL_UP,false,t);assert(m.row==m.rowCount-1);
    assert(m.input(SCE_CTRL_CIRCLE,false,t)==-1);
    assert(m.rowsTop+m.visibleRows*m.rowPitch<=m.overviewTop);
    std::filesystem::remove(path);
    std::filesystem::remove(emote_mesh_settings_path(folder,"../game"));
    std::filesystem::remove(dir);
    std::cout<<"PASS: backend availability, persistence, isolation, recovery, mesh preservation and menu controls\n";
}
