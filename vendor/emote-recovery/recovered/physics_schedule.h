#ifndef RECOVERED_PHYSICS_SCHEDULE_H
#define RECOVERED_PHYSICS_SCHEDULE_H
/* Isolated scheduling arithmetic from 811837A4 and 81183A00. The host must
 * resolve current anchors after model evaluation, and retain previous anchors.
 * This does not advance timelines or publish controller output variables. */
typedef struct {
    float frames, elapsed;
    float previous[2], current[2];
    unsigned first;
} EmPhysicsSchedule;
/* Initialize elapsed=0. Requires finite frames >= 0; original host skips
 * physics when total dt==0. first is the original record+4 initialization flag.
 * Returns 1 with dt/anchor for a step, 0 when no step remains (outputs intact).
 * No artificial frame cap. First initialization uses one whole-frame step. */
int em_physics_schedule_next(EmPhysicsSchedule *schedule, float *dt, float anchor[2]);
#endif
