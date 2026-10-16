#ifndef RECOVERED_MOTION_EVENTS_H
#define RECOVERED_MOTION_EVENTS_H
#include <stddef.h>
#include <stdint.h>
/* Decoded original tag/layer frame fields. Strings remain owned by the PSB. */
typedef struct { float time; uint32_t type, mask; int align, sync; const char *action; } EmMotionEventFrame;
typedef struct { uint32_t type; const char *layer, *action; } EmMotionEvent;
typedef struct { EmMotionEvent *records; size_t count, capacity; } EmMotionEventQueue;
typedef struct { const EmMotionEventFrame *frames; uint32_t count, index; const char *layer; int tag; } EmMotionEventCursor;
typedef struct { float time; int aligned, synced; } EmMotionEventPosition;
/* Exact original guard fragments, including unsigned count-2 arithmetic.
   Public seek/step validate count>=2 before calling them. */
int em_motion_event_forward(uint32_t count,uint32_t index,float next,float now);
int em_motion_event_reverse(uint32_t count,float current,float now);
int em_motion_event_push(EmMotionEventQueue *,EmMotionEvent);
int em_motion_event_prepend(EmMotionEventQueue *parent,EmMotionEventQueue *child);
int em_motion_event_dispatch(EmMotionEventQueue *,void (*notify)(void *,const EmMotionEvent *),void *);
/* seek selects only the destination interval; forward/reverse visit crossed intervals.
   Input must contain sorted frames plus the original terminal sentinel. Times below the first
   sentinel are rejected; allocation is caller-owned and never implicit. */
int em_motion_event_seek(EmMotionEventCursor *,EmMotionEventPosition *,EmMotionEventQueue *);
int em_motion_event_step(EmMotionEventCursor *,EmMotionEventPosition *,EmMotionEventQueue *,int reverse);
#endif
