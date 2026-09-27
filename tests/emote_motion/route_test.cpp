#include "../../host-direct/src/emote_route.hpp"
#include <cassert>
#include <limits>
int main(){
    direct::BuiltinEffects e;e.flags[3]=1;
    e.modelClip[0]=e.modelClip[1]=-1.e30f;e.modelClip[2]=e.modelClip[3]=1.e30f;
    for(float& c:e.corners)c=1;
    assert(direct::simple_emote_material(e,false));
    assert(!direct::simple_emote_material(e,true));
    for(unsigned corner=4;corner<16;++corner){
        auto gradient=e;gradient.corners[corner]=.5f;
        assert(!direct::simple_emote_material(gradient,false));
    }
    for(unsigned flag=0;flag<3;++flag){auto filter=e;filter.flags[flag]=1;assert(!direct::simple_emote_material(filter,false));}
    auto clipped=e;clipped.modelClip[2]=.8f;assert(!direct::simple_emote_material(clipped,false));
    auto wipe=e;wipe.wipe[2]=1;assert(!direct::simple_emote_material(wipe,false));
    auto invalid=e;invalid.corners[0]=std::numeric_limits<float>::quiet_NaN();assert(!direct::simple_emote_material(invalid,false));
    // Native modes retain their own output convention on the specialized path.
    for(int mode=0;mode<6;++mode){auto blend=e;blend.wipe[3]=float(mode);blend.modelX[3]=1;assert(direct::simple_emote_material(blend,false));}
}
