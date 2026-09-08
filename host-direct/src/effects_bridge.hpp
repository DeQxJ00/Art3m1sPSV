#pragma once
#include "effects.hpp"
#include <cstddef>
// repr(C) layout mirrored by core/src/backend/gxm/native_effects.rs.
struct EffectDraw {
    uint64_t texture,mask;
    float transform[6],quadSize[2],uv[4],tint[4],clip[4];
    direct::Effects effects;
    uint32_t blend,hasClip;
    const float* mesh;
    size_t meshCount;
};
static_assert(offsetof(EffectDraw,effects)==96);
static_assert(offsetof(EffectDraw,blend)==272);
static_assert(offsetof(EffectDraw,mesh)==280);
extern "C" {
void art3m1s_gxm_draw_effect(const EffectDraw*);
int art3m1s_gxm_group_begin();
int art3m1s_gxm_group_mask_begin();
void art3m1s_gxm_group_end(const EffectDraw*);
}
