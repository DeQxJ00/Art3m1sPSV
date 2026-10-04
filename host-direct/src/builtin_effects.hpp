#pragma once
#include <cstddef>
#include <cstdint>
namespace direct {
// Matches core/backend/gxm/native_effects.rs; rebuild both sides together.
struct BuiltinEffects {
    float flags[4]{}; // kind (sprite/rule/mask/group), gray, negative, E-mote
    float transition[4]{0,1.f/255,0,0};
    float corners[16]{}, uvRect[4]{0,0,1,1}, modelClip[4]{0,0,1,1};
    float wipe[4]{}, modelX[4]{}, modelY[4]{};
};
struct CustomDraw { uint32_t program=0; float values[128]{}; uint64_t userTexture=0; };
struct EffectDraw {
    uint64_t texture,mask;
    float transform[6],quad[2],uv[4],tint[4],clip[4];
    BuiltinEffects effects;
    uint32_t blend,hasClip;
    const float (*mesh)[4];
    size_t meshCount;
    CustomDraw custom;
    uint32_t gridSide=0;
};
static_assert(sizeof(BuiltinEffects)==176);
static_assert(offsetof(EffectDraw,effects)==96);
static_assert(offsetof(EffectDraw,blend)==272);
static_assert(offsetof(EffectDraw,gridSide)==offsetof(EffectDraw,custom)+sizeof(CustomDraw));
}
