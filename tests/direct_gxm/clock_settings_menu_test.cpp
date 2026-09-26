#include "../../host-direct/src/clock_settings_menu.hpp"
#include <cassert>
int main(){
    direct::ClockSettingsMenu menu;SceTouchData touch{};
    for(int i=0;i<4;i++)assert(menu.input(SCE_CTRL_DOWN,false,touch)==0);
    assert(menu.row==4&&menu.value.effectPanCpu==0);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==0&&menu.value.effectPanCpu==444);
    assert(menu.input(SCE_CTRL_LEFT,false,touch)==0&&menu.value.effectPanCpu==0);
    assert(menu.input(SCE_CTRL_RIGHT,false,touch)==0&&menu.value.effectPanCpu==444);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==5);
    assert(menu.input(SCE_CTRL_LEFT,false,touch)==0&&menu.value.effectPanEs4==222);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==6);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==1);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==0);
    menu.input(SCE_CTRL_UP,false,touch);assert(menu.row==6);
    touch.reportNum=1;touch.report[0].x=700*2;touch.report[0].y=278*2;
    assert(menu.input(0,true,touch)==0&&menu.row==4&&menu.value.effectPanCpu==0);
    touch.report[0].y=354*2;assert(menu.input(0,true,touch)==1&&menu.row==6);
    assert(menu.input(SCE_CTRL_CROSS,false,touch)==-1);
}
