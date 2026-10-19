#include "../../host-direct/src/resolution_notice.hpp"
#include <cassert>
int main(){
 using namespace direct;
 assert(!ResolutionNotice::needed({960,540}));assert(!ResolutionNotice::needed({960,544}));
 assert(ResolutionNotice::needed({1280,720}));assert(ResolutionNotice::needed({640,360}));
 std::string ini="[VITA]\nWIDTH=960\nHEIGHT=540\n[ANDROID]\nWIDTH=1280\nHEIGHT=720\n";
 ResolutionNotice n;n.check(platform_resolution(ini,"ANDROID"));assert(n.open);
 SceTouchData touch{};
 assert(n.input(SCE_CTRL_CIRCLE,touch)==0);assert(n.input(0,touch)==0);
 assert(n.input(SCE_CTRL_CIRCLE,touch)==1);
 n.check({1280,720});touch.reportNum=1;touch.report[0].x=1200;touch.report[0].y=880;
 assert(n.input(0,touch)==0);touch.reportNum=0;assert(n.input(0,touch)==0);
 touch.reportNum=1;assert(n.input(0,touch)==1);touch.report[0].x=400;assert(n.input(0,touch)==-1);
 touch.reportNum=0;assert(n.input(SCE_CTRL_CROSS,touch)==-1);
 n.check(platform_resolution(ini,"VITA"));assert(!n.open);
 n.check_table("list_vita.tbl","init={game_scale={1280,720},game_width=960,game_height=540}");assert(!n.open&&n.tableSize.known());
 n.check_table("list_vita.tbl","init={game_width=1280,game_height=720}");assert(n.open);
 auto t=table_resolution("-- game_width=1920\n--[=[game_height=1080]=]\nlocal s='game_width=1';init={game_width=960,game_height=544}");
 assert(t.known()&&t.width==960&&t.height==544);
 assert(!table_resolution("init={game_width=960*2,game_height=540}").known());
 assert(!table_resolution("init={game_width=960,game_height=540};init.game_width=1280").known());
 assert(!table_resolution("init={game_width=dynamic(),game_height=540}").known());
 assert(!table_resolution("init={game_width=960\n *2,game_height=540}").known());
 n.check({960,540});n.check_table("list_android.tbl","");assert(!n.open&&!n.tableSize.known());
}
