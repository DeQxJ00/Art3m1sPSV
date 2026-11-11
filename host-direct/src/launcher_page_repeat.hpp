#pragma once
#include <cstdint>

namespace direct {
class LauncherPageRepeat {
public:
    int direction()const{return direction_;}
    int update(int direction,bool pressed,uint64_t now){
        if(pressed&&direction){direction_=direction;next_=now+300000;return direction;}
        if(direction!=direction_){direction_=0;next_=0;return 0;}
        if(!next_||now<next_)return 0;
        next_=now+90000;
        return direction;
    }
private:
    int direction_=0;
    uint64_t next_=0;
};
}
