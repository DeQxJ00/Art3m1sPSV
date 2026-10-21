#ifndef MODEL_CONTROLS_H
#define MODEL_CONTROLS_H
#include "psb.h"
typedef struct EmControlsContext EmControlsContext;
/* Select with the matching timeline context before update or evaluation.
 * Thread-local scoped selection; NULL selects the legacy default instance.
 * PSB storage must outlive this context. Destroying the active instance
 * selects the default; saved handles must not outlive their instances. */
EmControlsContext *model_controls_context_create(void);
EmControlsContext *model_controls_context_use(EmControlsContext *context);
void model_controls_context_destroy(EmControlsContext *context);
/* Borrowed during a frozen evaluation only. Missing values still consult the
 * selected matching timeline; do not mutate this context concurrently. */
EmControlsContext *model_controls_context_current(void);
float model_controls_value_in(const EmControlsContext *context,const char *id);
/* Invalidates name-demand caches on metadata rebinding. */
unsigned model_controls_anchor_binding_in(const EmControlsContext *context);
int model_controls_needs_anchor_in(const EmControlsContext *context,const char *label);
void model_controls_bind(EmPsbValue root);
/* Initialization only, after bind/load, before playback. Owner supplies a
 * distinct instance seed; runtime updates never share random state. */
void model_controls_seed(uint64_t seed);
void model_controls_range(const char *id,float begin,float end);
void model_controls_step(float frames);
void model_controls_reset_physics(void);
/* Two-phase original ordering: step prepares parameters from the previously
 * published physics outputs; begin/candidate/finish surround model evaluation.
 * finish consumes pending delta exactly once and publishes for the next step.
 * World anchors exclude screen fitting. Host state resets to identity on bind. */
/* Evaluated model origin/angle, inverse of player scale * transition scale,
 * and independent player bust/hair/parts amplitudes. Not the screen-fit view.
 * Set before candidates; current pose angle is used for all physics substeps. */
/* Separate pose updates preserve persistent host amplitudes/scale. root here
 * is the synthetic original root, NOT the first authored PSB layer. Caller
 * supplies effective angle after any host override. */
void model_controls_pose_root(float origin_x,float origin_y,float angle_degrees);
void model_controls_set_physics_scales(float inverse_scale,float bust_scale,float hair_scale,float parts_scale);
void model_controls_set_physics_host(float origin_x,float origin_y,float inverse_scale,
                                    float angle_degrees,float bust_scale,float hair_scale,float parts_scale);
/* Name demand only: do not reject already-seen or invalid first matches. */
int model_controls_needs_anchor(const char *label);
void model_controls_begin_pose(void);
void model_controls_anchor_candidate(const char *label,float x,float y,int valid);
void model_controls_finish_pose(void);
float model_controls_value(const char *id);
void model_controls_anchor(const char *label,float x,float y);
int model_controls_talk_available(void);
void model_controls_talk_toggle(void);
int model_controls_talking(void);
void model_controls_mouth_open_toggle(void);
int model_controls_mouth_open(void);
int model_controls_blink_available(void);
void model_controls_blink_now(void);
void model_controls_wind_toggle(void);
int model_controls_windy(void);
int model_controls_has(const char *id);
#endif
