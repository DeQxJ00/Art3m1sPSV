#include "physics_schedule.h"
int em_physics_schedule_next(EmPhysicsSchedule *s, float *dt, float anchor[2]) {
    if (s->frames == 0.0f) return 0; /* 811850DE: physics calls omitted */
    if (s->first) {
        s->first = 0;
        s->elapsed = s->frames;
        *dt = s->frames;
        anchor[0] = s->current[0];
        anchor[1] = s->current[1];
        return 1;
    }
    /* 0x34000000, loaded before the loop in both original controllers. */
    if (!(s->elapsed < s->frames - 0x1p-23f)) return 0;
    float step = s->frames - s->elapsed;
    if (step > 1.1f) step = 1.1f;
    s->elapsed = s->elapsed + step;
    float t = s->elapsed / s->frames;
    float rest = 1.0f - t;
    /* Preserve current*t + previous*(1-t), not previous+(current-previous)*t. */
    anchor[0] = s->current[0] * t + s->previous[0] * rest;
    anchor[1] = s->current[1] * t + s->previous[1] * rest;
    *dt = step;
    return 1;
}
