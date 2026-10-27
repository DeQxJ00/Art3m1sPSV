#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
int main(){
    using namespace direct;
    const auto root=std::filesystem::temp_directory_path()/"art3-story-lookahead-test";
    std::filesystem::create_directories(root);
    assert(!load_story_lookahead_settings(root.string(),"sample01"));
    assert(save_story_lookahead_settings(root.string(),"sample01",true));
    assert(load_story_lookahead_settings(root.string(),"sample01"));
    assert(!load_story_lookahead_settings(root.string(),"sample02"));
    GameSettingsMenu menu;SceTouchData touch{};
    menu.row=12;assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==16&&!menu.storyLookahead);
    assert(menu.input(SCE_CTRL_RIGHT,false,touch)==16);
    touch.reportNum=1;touch.report[0].x=700*2;
    touch.report[0].y=(menu.rowsTop+5*menu.rowPitch+15)*2;
    assert(menu.input(0,true,touch)==16&&menu.row==12);
    menu.row=13;assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==-1);
    assert(save_story_lookahead_settings(root.string(),"sample01",false));
    assert(!load_story_lookahead_settings(root.string(),"sample01"));
    std::filesystem::remove(story_lookahead_settings_path(root.string(),"sample01"));
    std::filesystem::remove(root);
}
