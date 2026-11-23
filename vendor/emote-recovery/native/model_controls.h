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
void model_controls_assign(EmControlsContext *target,const EmControlsContext *source,int same_model);
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
/* Scale channels follow the physics group order: bust, hair, parts. */
void model_controls_set_physics_scale(unsigned group,float scale);
float model_controls_physics_scale(unsigned group);
/* Sampled player transitions. Forces use model units, grouped as above.
 * Outer rotation offsets physics gravity in degrees. */
void model_controls_set_outer_force(unsigned group,float x,float y);
void model_controls_set_outer_rotation(float degrees);
/* Query authored PSB presentation metadata. Clear color uses 0xRRGGBB00. */
uint32_t model_controls_suitable_clear_color(void);
float model_controls_authored_scale(void);
/* Projection for the selected logical screen. Caller resolves mirrored
 * screen order and combines the volume and parallax ratio. */
void model_controls_set_stereovision(int enabled,float coefficient,unsigned index,unsigned screen_count);
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
/* Gate autonomous blinks. Refresh model_controls_step(0) after changing
 * this option if the model must be rendered before the next tick. */
void model_controls_set_blink_enabled(int enabled);
void model_controls_blink_now(void);
void model_controls_wind_toggle(void);
int model_controls_windy(void);
int model_controls_start_wind(float start,float end,float speed,float power_a,float power_b,float player_scale);
void model_controls_stop_wind(void);
int model_controls_has(const char *id);
int model_controls_has_state_variable(const char *id);
#endif
