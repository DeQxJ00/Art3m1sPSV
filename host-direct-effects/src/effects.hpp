#pragma once
#include <cstdint>
namespace direct {
// Numeric blend codes are shared with core/backend/gxm/native.rs. The first
// five retain the original bridge ABI; native modes preserve destination alpha.
enum Blend : unsigned { Alpha, Add, Multiply, Screen, ReverseSubtract,
    PremultipliedAlpha, PremultipliedAdd, NativeAdd, NativeMultiply, NativeScreen, Copy, BlendCount };
enum EffectKind : unsigned { Sprite, Rule, AlphaMask, GroupComposite };
// All-float, zero-initialized blocks match the shader's float4 uniform arrays.
// flags: kind, grayscale, negative, native-emote enabled.
struct Effects {
    float flags[4]{};
    float transition[4]{0,1.0f/255,0,0}; // progress, vague, opaque, unused
    float corners[16]{};
    float uvRect[4]{0,0,1,1};
    float modelClip[4]{0,0,1,1};
    float wipe[4]{}; // scale, bias, enabled, native blend code
    float modelX[4]{1,0,0,0},modelY[4]{0,1,0,0}; // display pixels -> model coords
};
static_assert(sizeof(Effects)==44*sizeof(float));
}
