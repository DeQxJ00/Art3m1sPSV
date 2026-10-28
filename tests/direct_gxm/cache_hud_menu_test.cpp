#include "../../host-direct/src/launcher_settings_menu.hpp"
#include <cassert>
int main(){
    direct::LauncherSettingsMenu m;SceTouchData touch{};
    for(int row=0;row<10;++row){
        m.row=row;
        const int expected=row==9?-1:row+1;
        assert(m.input(SCE_CTRL_CIRCLE,false,touch)==expected);
        if(row!=1&&row!=9){
            assert(m.input(SCE_CTRL_LEFT,false,touch)==expected);
            assert(m.input(SCE_CTRL_RIGHT,false,touch)==expected);
        }
        touch.reportNum=1;touch.report[0].x=400*2;touch.report[0].y=(106+row*31+15)*2;
        m.row=0;assert(m.input(0,true,touch)==expected&&m.row==row);
    }
    assert(m.row==9);m.input(SCE_CTRL_DOWN,false,touch);assert(m.row==0);
    m.input(SCE_CTRL_UP,false,touch);assert(m.row==9);
    assert(m.input(SCE_CTRL_START,false,touch)==-1);
    touch.report[0].y=416*2;assert(m.input(0,true,touch)==0);
}
