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
    assert(menu.value.emoteCpu==444&&menu.value.emoteEs4==222);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==0&&menu.value.emoteCpu==0);
    menu.input(SCE_CTRL_RIGHT,false,touch);assert(menu.value.emoteCpu==444);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==7);
    menu.input(SCE_CTRL_LEFT,false,touch);assert(menu.value.emoteEs4==166);
    menu.input(SCE_CTRL_RIGHT,false,touch);assert(menu.value.emoteEs4==222);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==8&&menu.first_row()==1);
    assert(menu.value.particleCpu==444&&menu.value.particleEs4==222);
    menu.input(SCE_CTRL_CIRCLE,false,touch);assert(menu.value.particleCpu==0);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==9&&menu.first_row()==2);
    menu.input(SCE_CTRL_LEFT,false,touch);assert(menu.value.particleEs4==166);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==10&&menu.first_row()==3);
    assert(menu.value.maskCpu==444&&menu.value.maskEs4==222);
    menu.input(SCE_CTRL_CIRCLE,false,touch);assert(menu.value.maskCpu==0);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==11);
    menu.input(SCE_CTRL_LEFT,false,touch);assert(menu.value.maskEs4==166);
    menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==12);
    assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==1);
    touch.reportNum=1;touch.report[0].x=700*2;touch.report[0].y=(104+7*32+16)*2;
    assert(menu.input(0,true,touch)==1&&menu.row==12);
    touch.report[0].y=(104+5*32+16)*2;
    assert(menu.input(0,true,touch)==0&&menu.row==10&&menu.value.maskCpu==444);
    menu.row=12;menu.input(SCE_CTRL_DOWN,false,touch);assert(menu.row==0&&menu.first_row()==0);
    menu.input(SCE_CTRL_UP,false,touch);assert(menu.row==12);
    touch.report[0].x=830*2;touch.report[0].y=120*2;
    assert(menu.input(0,true,touch)==0&&menu.row==4);
    touch.report[0].y=340*2;assert(menu.input(0,true,touch)==0&&menu.row==12);
    assert(menu.input(SCE_CTRL_CROSS,false,touch)==-1);
}
