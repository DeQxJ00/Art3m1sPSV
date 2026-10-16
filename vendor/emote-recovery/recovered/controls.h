/* Original PCSG00341 control layouts; names reconstructed from RTTI/code. */
#ifndef RECOVERED_CONTROLS_H
#define RECOVERED_CONTROLS_H
#include <stdint.h>
#include <stddef.h>
typedef struct { float start, end, duration; } EmLoopSegment;
typedef struct {
    uint32_t vtable_tag;
    int32_t segment;
    float elapsed;
    uint32_t vector_reserved;
    const EmLoopSegment *begin, *end, *capacity_end;
} EmLoop;
typedef struct { float target[4], duration, exponent; } EmTransitionCommand;
typedef struct {
    uint32_t vtable_tag, reserved;
    EmTransitionCommand **queue;
    uint32_t capacity, head, queued, dimensions;
    int32_t state;
    float *current, *start, *target;
    float exponent, progress, inverse_duration;
} EmTransition;
_Static_assert(sizeof(EmLoop)==28, "original loop size");
_Static_assert(sizeof(EmTransition)==56, "original transition size");
_Static_assert(offsetof(EmTransition,progress)==48, "original transition fields");
/* Requires nonempty segment array and positive durations, like original. */
void em_loop_step(EmLoop *control, float *out, float frames); /* 8118891A */
/* 1..4 dimensions; queue storage/initialization owned by caller. */
void em_transition_step(EmTransition *control, float *out, float frames); /* 81189FFA */
int em_transition_active(const EmTransition *control); /* 8118A6D8 */
/* 81186096..81186164: spring integration after anchor/rotated gravity
 * resolution. Caller owns initialization, angle conversion and output clamps. */
void em_bust_spring(float position[3], float velocity[3], const float anchor[3],
                    float spring, float friction, float gravity,
                    float gravity_x, float gravity_y, float frames);
/* Host transform/feedback arithmetic from 811836FA/81184926/811972BC. */
float em_physics_angle_radians(float degrees);
float em_physics_inverse_scale(float player_scale,float transition_scale);
void em_physics_normalize_anchor(float out[2],float x,float y,float origin_x,float origin_y,float inverse_scale);
void em_physics_rotate_gravity(float out[2],float base_x,float base_y,float sine,float cosine);
/* Original 8118A6EC input/output around imported atanf; libm is external. */
float em_physics_limit_argument(float value);
float em_physics_limit_result(float arctangent);
float em_physics_limit(float value);
#endif
