#include "readback.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
    using namespace direct;
    assert(game_viewport(960,544).full());assert(game_viewport(1280,720).full());
    for(const auto size:{std::pair<unsigned,unsigned>{720,540},{1024,768},{540,960},{2048,512}}){
        auto v=game_viewport(size.first,size.second);
        assert(std::abs(v.w/v.h-float(size.first)/size.second)<.0001f);
        assert(std::abs(v.x*2+v.w-960)<.001f&&std::abs(v.y*2+v.h-544)<.001f);
        int x=-1,y=-1;
        assert(v.touch(960,544,size.first,size.second,x,y));
        assert(std::abs(x-int(size.first/2))<=1&&std::abs(y-int(size.second/2))<=1);
        assert(!v.touch(0,0,size.first,size.second,x,y));
        // A constant content color surrounded by black must survive thumbnail
        // readback without accumulating bars on each transition/capture.
        std::vector<uint8_t> src(1024*544*4),out(320*240*4);
        for(unsigned yy=0;yy<544;++yy)for(unsigned xx=0;xx<960;++xx)
            if(xx+.5f>=v.x&&xx+.5f<v.x+v.w&&yy+.5f>=v.y&&yy+.5f<v.y+v.h)
                for(unsigned c=0;c<4;++c)src[(yy*1024+xx)*4+c]=uint8_t(50+c*30);
        copy_game_frame(src.data(),320,240,out.data(),v);
        for(unsigned i=0;i<out.size();++i)assert(out[i]==50+(i%4)*30);
    }
    auto v=game_viewport(720,540);int x=0,y=0;
    assert(v.touch(240,4,720,540,x,y)&&x<4&&y<4);
    assert(!v.touch(233,540,720,540,x,y));
    assert(!v.touch(1686,540,720,540,x,y));
    std::puts("PASS viewport aspect, touch boundaries, native widescreen and cropped snapshots");
}
