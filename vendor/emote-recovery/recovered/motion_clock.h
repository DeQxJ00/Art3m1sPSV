#ifndef RECOVERED_MOTION_CLOCK_H
#define RECOVERED_MOTION_CLOCK_H
/* State/operations reconstructed from PCSG00341 8119C54E. Adapter layout,
 * not an original ABI. Handler updates time/absolute and sync/align on tags. */
typedef struct {float absolute,time,last,loop;unsigned playing,seek,from_edge,hold,synced,aligned;} EmMotionClock;
enum { EM_MOTION_SEEK,EM_MOTION_FORWARD,EM_MOTION_REVERSE };
typedef int (*EmMotionVisit)(void *,EmMotionClock *,int);
/* Malformed/non-finite clocks and non-progressing or >65536-wrap requests fail. */
int em_motion_clock_step(EmMotionClock *,float delta,EmMotionVisit,void *);
#endif
