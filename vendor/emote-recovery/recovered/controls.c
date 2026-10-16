#include "controls.h"
#include <math.h>

void em_loop_step(EmLoop *c, float *out, float frames) {
    float elapsed = c->elapsed + frames;
    c->elapsed = elapsed;
    int32_t index = c->segment;
    if (elapsed >= c->begin[index].duration) {
        int32_t count = (int32_t)(c->end - c->begin);
        do {
            elapsed = elapsed - c->begin[index].duration;
            index = (index + 1) % count;
        } while (elapsed >= c->begin[index].duration);
        c->segment = index;
        c->elapsed = elapsed;
    }
    const EmLoopSegment *s = c->begin + index;
    float t = elapsed / s->duration;
    /* Preserve original VMUL/VMLA order, not start+(end-start)*t. */
    float value = s->end * t;
    *out = value + s->start * (1.0f - t);
}

void em_transition_step(EmTransition *c, float *out, float frames) {
    if (c->state == 0 && c->queued) {
        EmTransitionCommand command = *c->queue[c->head & (c->capacity-1)];
        --c->queued;
        c->head = c->queued ? c->head+1 : 0;
        for (uint32_t i=0;i<c->dimensions;++i) {
            c->start[i] = c->current[i];
            c->target[i] = command.target[i];
        }
        c->progress = 0.0f;
        c->exponent = command.exponent;
        ++c->state;
        c->inverse_duration = 1.0f / command.duration;
        /* Original deliberately consumes no frame time on dequeue. */
    } else if (c->state == 1) {
        float progress = c->progress + frames * c->inverse_duration;
        if (progress >= 1.0f) {
            c->progress = 1.0f;
            for (uint32_t i=0;i<c->dimensions;++i) c->current[i]=c->target[i];
            --c->state;
        } else {
            c->progress=progress;
            float weight=powf(progress,c->exponent); /* original import 812162CC */
            for (uint32_t i=0;i<c->dimensions;++i)
                c->current[i]=c->start[i]+(c->target[i]-c->start[i])*weight;
        }
    }
    for (uint32_t i=0;i<c->dimensions;++i) out[i]=c->current[i];
}
int em_transition_active(const EmTransition *c) {
    return c->state != 0 || c->queued != 0;
}

void em_bust_spring(float position[3], float velocity[3], const float anchor[3],
                    float spring, float friction, float gravity,
                    float gravity_x, float gravity_y, float frames) {
    /* VMLA in this ARMv7 path rounds the product before addition. Keep the
     * spring/damping coefficients multiplied by dt before applying them. */
    float stiffness = spring * frames;
    float gravity_step = gravity * frames;
    float damping = friction * frames;
    const float direction[3] = {gravity_x, gravity_y, 0.0f};
    for (unsigned i = 0; i != 3; ++i) {
        float v = velocity[i] + (anchor[i] - position[i]) * stiffness;
        v = v + direction[i] * gravity_step;
        v = v - damping * v;
        velocity[i] = v;
        position[i] = position[i] + frames * v;
    }
}

/* 811972D0..EA; preserve two rounding points rather than degrees*pi/180. */
float em_physics_angle_radians(float degrees) {return (degrees * 0x1.921fb6p+2f) / 360.0f;}
/* 81184A08/12 and 81183764..86 respectively. */
float em_physics_inverse_scale(float player_scale,float transition_scale) {return 1.0f/(player_scale*transition_scale);}
void em_physics_normalize_anchor(float out[2],float x,float y,float origin_x,float origin_y,float inverse_scale) {
    out[0]=(x-origin_x)*inverse_scale+origin_x;
    out[1]=(y-origin_y)*inverse_scale+origin_y;
}
/* 811860AE..E2, sin/cos are _FSinx(-angle,0/1,0) results. */
void em_physics_rotate_gravity(float out[2],float base_x,float base_y,float sine,float cosine) {
    out[0]=cosine*base_x-sine*base_y;
    out[1]=cosine*base_y+sine*base_x;
}
/* 8118A6EC: keep Sony libm boundary explicit for differential tests. */
float em_physics_limit_argument(float value) {return (value*0x1.266666p+0f)*0x1.41b2f8p-5f;}
float em_physics_limit_result(float arctangent) {return arctangent/0x1.41b2f8p-5f;}
float em_physics_limit(float value) {return em_physics_limit_result(atanf(em_physics_limit_argument(value)));}
