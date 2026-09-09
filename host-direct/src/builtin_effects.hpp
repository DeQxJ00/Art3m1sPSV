#pragma once
#include <cstddef>
#include <cstdint>
namespace direct {
// Matches core/backend/gxm/native_effects.rs. Existing sprite ABI stays valid.
struct BuiltinEffects {
    float flags[4]{}; // kind (sprite/rule/mask/group), gray, negative, E-mote
    float transition[4]{0,1.f/255,0,0};
    float corners[16]{}, uvRect[4]{0,0,1,1}, modelClip[4]{0,0,1,1};
    float wipe[4]{}, modelX[4]{}, modelY[4]{};
};
struct EffectDraw {
    uint64_t texture,mask;
    float transform[6],quad[2],uv[4],tint[4],clip[4];
    BuiltinEffects effects;
    uint32_t blend,hasClip;
    const float (*mesh)[4];
    size_t meshCount;
};
static_assert(sizeof(BuiltinEffects)==176);
static_assert(offsetof(EffectDraw,effects)==96);
static_assert(offsetof(EffectDraw,blend)==272);
}
