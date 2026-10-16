#ifndef VIEW_TIMELINE_H
#define VIEW_TIMELINE_H
#include "psb.h"
typedef struct EmTimelineContext EmTimelineContext;
/* One evaluator per player; PSB bytes must outlive the context.
 * use(NULL) selects the compatibility/default instance; returns previous.
 * Selection is thread-local; distinct contexts may run concurrently. */
EmTimelineContext *timeline_context_create(void);
EmTimelineContext *timeline_context_use(EmTimelineContext *context);
void timeline_context_destroy(EmTimelineContext *context);
void timeline_bind(EmPsbValue root);
/* Host binding clears prior playback/variables without selecting a demo action. */
void timeline_bind_neutral(EmPsbValue root);
/* Named-script host: no demo action list/classification. Playback is unchanged. */
void timeline_bind_script(EmPsbValue root);
void timeline_select(int index);
/* Dynamic named timelines, independent of UI categories. Known call flags:
 * bit 0 parallel; bit 1 differential. Other flags rejected until recovered. */
int timeline_play(const char *label,unsigned flags);
/* Metadata lookup only; does not start, stop or allocate a timeline. */
int timeline_has_label(const char *label);
int timeline_stop(const char *label); /* empty name stops all timelines */
/* Empty name queries any playing lane; NULL is invalid and returns false. */
int timeline_is_playing(const char *label);
int timeline_active_count(void);
void timeline_pass(void);
/* Native host command Step (skip=0) / Skip (skip=1), not Tick(delta).
 * Finish variables after physics reset, matching the player finalizer order. */
int timeline_finish_playback(int skip);
void timeline_finish_variables(void);
int timeline_seek_named(const char *label,float frame);
int timeline_fade(const char *label,float target,float duration,float easing,int remove_after);
int timeline_fade_in(const char *label,float duration,float easing);
void timeline_seek(float frame);
void timeline_step(float frames);
/* Original transition command semantics; duration is frames, easing is PSB
 * easing. append=0 replaces pending/active work; duration<=0 sets immediately.
 * Names are copied. Returns 0 for invalid input, allocation failure or full
 * parameter table. PSV normal channels issue commands at key crossings; Win
 * retains adapter arbitration. Complete registry/order parity is not claimed. */
int timeline_set_variable(const char *name,float value,float duration,float easing,int append);
/* Original transition finish: last queued target, otherwise active target. */
int timeline_finish_variable(const char *name);
/* Physical/controller feedback writes the normal map without cancelling its
 * queued controller. A later active normal timeline may overwrite this value. */
int timeline_publish_control(const char *name,float value);
float timeline_value(const char *name);
float timeline_time(void);
float timeline_length(void);
int timeline_index(void);
int timeline_count(void);
int timeline_looping(void);
int timeline_finished(void);
const char *timeline_label(void);
int timeline_category(int index);
int timeline_category_count(int category);
int timeline_category_position(void);
void timeline_next(int category,int direction);
const char *timeline_category_name(int category);
void timeline_filter(int (*available)(const char *));
void timeline_focus_category(int category);
#endif
