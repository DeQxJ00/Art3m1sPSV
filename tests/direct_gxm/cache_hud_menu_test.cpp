#include "../../host-direct/src/launcher_settings_menu.hpp"
#include <cassert>
int main(){
    direct::LauncherSettingsMenu m;SceTouchData touch{};
    for(int i=0;i<4;++i)assert(m.input(SCE_CTRL_DOWN,false,touch)==0);
    assert(m.row==4);assert(m.input(SCE_CTRL_CIRCLE,false,touch)==5);
    assert(m.input(SCE_CTRL_LEFT,false,touch)==5);
    assert(m.input(SCE_CTRL_RIGHT,false,touch)==5);
    m.input(SCE_CTRL_DOWN,false,touch);assert(m.row==5);
    assert(m.input(SCE_CTRL_CIRCLE,false,touch)==6);
    assert(m.input(SCE_CTRL_RIGHT,false,touch)==6);
    m.input(SCE_CTRL_DOWN,false,touch);assert(m.row==6);
    assert(m.input(SCE_CTRL_CIRCLE,false,touch)==-1);
    m.input(SCE_CTRL_DOWN,false,touch);assert(m.row==0);
    m.input(SCE_CTRL_UP,false,touch);assert(m.row==6);
    touch.reportNum=1;touch.report[0].x=400*2;touch.report[0].y=292*2;
    assert(m.input(0,true,touch)==5&&m.row==4);
    touch.report[0].y=334*2;assert(m.input(0,true,touch)==6&&m.row==5);
    touch.report[0].y=376*2;assert(m.input(0,true,touch)==-1&&m.row==6);
    assert(m.input(SCE_CTRL_START,false,touch)==-1);
}
