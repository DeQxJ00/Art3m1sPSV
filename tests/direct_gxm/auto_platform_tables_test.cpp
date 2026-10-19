#include "../../host-direct/src/game_settings_menu.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
int main(){
 using namespace direct;
 const auto root=std::filesystem::temp_directory_path()/"art3-auto-platform-tables-test";
 std::filesystem::create_directories(root);
 assert(load_auto_platform_tables_settings(root.string(),"sample01"));
 assert(save_auto_platform_tables_settings(root.string(),"sample01",false));
 assert(!load_auto_platform_tables_settings(root.string(),"sample01"));
 assert(load_auto_platform_tables_settings(root.string(),"sample02"));
 assert(!load_resolution_override(root.string(),"sample01",false));
 assert(!load_resolution_override(root.string(),"sample01",true));
 assert(save_resolution_override(root.string(),"sample01",false,true));
 assert(load_resolution_override(root.string(),"sample01",false));
 assert(!load_resolution_override(root.string(),"sample01",true));
 assert(!load_resolution_override(root.string(),"sample02",false));
 std::string ini="[WINDOWS]\nWIDTH=1920\nHEIGHT=1080\nBOOT=main\n";
 assert(add_platform_section(ini,"SWITCH"));
 auto size=platform_resolution(ini,"SWITCH");assert(size.width==1920&&size.height==1080);
 override_ini_resolution(ini,"SWITCH");size=platform_resolution(ini,"SWITCH");assert(size.width==960&&size.height==540);
 size=platform_resolution(ini,"WINDOWS");assert(size.width==1920&&size.height==1080);
 GameSettingsMenu menu;SceTouchData touch{};menu.row=9;
 assert(menu.autoPlatformTables&&menu.input(SCE_CTRL_CIRCLE,false,touch)==13);
 menu.row=GameSettingsMenu::rowCount-1;assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==-1);
 assert(menu.rowsTop+menu.visibleRows*menu.rowPitch<=menu.overviewTop);
 menu.row=0;menu.input(SCE_CTRL_RTRIGGER,false,touch);assert(menu.row==7);
 menu.input(SCE_CTRL_RTRIGGER,false,touch);assert(menu.row==0);
 menu.input(SCE_CTRL_LTRIGGER,false,touch);assert(menu.row==7);
 menu.row=7;touch.reportNum=1;touch.report[0].x=700*2;touch.report[0].y=(menu.rowsTop+3*menu.rowPitch+15)*2;
 assert(menu.input(0,true,touch)==14&&menu.row==10&&!menu.forceIni&&!menu.forceTable);
 std::filesystem::remove(resolution_override_path(root.string(),"sample01",false));
 std::filesystem::remove(auto_platform_tables_settings_path(root.string(),"sample01"));std::filesystem::remove(root);
}
