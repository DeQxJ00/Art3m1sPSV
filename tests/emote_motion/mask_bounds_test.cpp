#include "../../host-direct/src/emote_mask_bounds.hpp"
#include <cassert>
#include <limits>
int main(){
    direct::EmoteMaskBounds b;float r[4];assert(!b.rectangle(r));
    b.reset();assert(!b.rectangle(r));
    direct::Vertex triangle[3]{};
    triangle[0].x=100.25f;triangle[0].y=200.75f;
    triangle[1].x=240.75f;triangle[1].y=180.25f;
    triangle[2].x=190.5f;triangle[2].y=302.5f;
    b.include(triangle,3,true);assert(b.rectangle(r));
    assert(r[0]==99&&r[1]==179&&r[2]==242&&r[3]==304);
    triangle[0].x=-40;triangle[0].y=550;
    b.include(triangle,3,true);assert(b.rectangle(r));
    assert(r[0]==0&&r[3]==544);
    b.include(triangle,3,false);assert(!b.rectangle(r));
    b.include(triangle,3,true);assert(!b.rectangle(r));
    b.reset();triangle[1].x=std::numeric_limits<float>::quiet_NaN();
    b.include(triangle,3,true);assert(!b.rectangle(r));
    b.reset();for(auto& v:triangle){v.x=1000;v.y=100;}
    b.include(triangle,3,true);assert(!b.rectangle(r));
}
