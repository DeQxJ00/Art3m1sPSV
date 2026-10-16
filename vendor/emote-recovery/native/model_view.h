#ifndef MODEL_VIEW_H
#define MODEL_VIEW_H
#include "psb.h"
#include "scene_runtime.h"
#include "texture_host.h"
#include "motion_events.h"
typedef struct EmViewContext EmViewContext;
/* Thread-local selection with matching timeline/control contexts. GPU must
 * remain alive during destruction; destroy only outside an open GPU scene.
 * PSB data must outlive the view. NULL selects the legacy default. */
EmViewContext *model_view_context_create(void);
EmViewContext *model_view_context_use(EmViewContext *context);
void model_view_context_destroy(EmViewContext *context);
/* Explicit model-space -> pixel presentation on the existing 960x544 GPU
 * canvas: pixel = model * scale + (x,y). No auto-fit. Finite inputs only;
 * returns 0 without changing state if invalid. Signed/zero scale is allowed.
 * Per-instance configuration survives load/free, just like layout. This does
 * not alter animation/physics transforms or provide nested render targets. */
int model_view_projection(float scale,float x,float y);
/* Fit the selected instance to a pixel box; zero size restores demo layout.
 * Always exits explicit projection, including when the box is unchanged. */
void model_view_layout(float x,float y,float width,float height);
/* Explicit provenance: 0 adapter/unknown, 1 verified PCSG00341 ordinary materials. */
void model_view_material_profile(int profile);
/* Per-view host choice: 0 hard counter (default), 1 original soft shaders.
 * Returns 0 for unsupported mode/profile. */
int model_view_mask_mode(int mode);
int model_view_get_mask_mode(void);
/* Set only while no textures are loaded. Copied callbacks/user must outlive
 * all snapshots. NULL selects demo GPU. Host mode requires capture. */
int model_view_texture_host(const EmTextureHost *host);
int model_view_texture_source_lease(const EmTextureSourceLease *lease);
int model_view_load(EmPsbValue root);
/* Immutable preparation has no view, controls, texture upload or GXM state. */
int model_view_compile_scene(EmScene *out,EmPsbValue root,size_t limit);
int model_view_load_precompiled(EmPsbValue root,const EmScene *prototype);

void model_view_free(void);
/* Global serial-cache counts, for resource ownership diagnostics. */
/* Cumulative main-thread periodic logging costs; never subtracted from FPS. */
void model_view_log_stats(long long *time_us,unsigned *opens,unsigned *errors);
void model_view_texture_stats(unsigned *resources,unsigned *owners);
/* Advance the independent root motion clock once per outer frame (not each
 * parameter transition substep). seek is explicit, not timeline parameter seek. */
void model_view_motion_step(float frames);
void model_view_motion_seek(float frame);
/* 1 event, 0 empty, -1 event failure. Borrowed PSB strings survive until load/free.
 * type0: layer/action; type1 sync: both strings NULL. Consume after draw. */
int model_view_poll_event(EmMotionEvent *event);
void model_view_draw(uint32_t *pixels,int width,int height,float eye,float mouth);
typedef struct EmRenderPacket EmRenderPacket;
/* Evaluate once and record owned exact960x544/NDC draw data without GXM calls.
 * Requires a successfully loaded view and serial render-thread use. Packet
 * survives view/PSB destruction via independent texture leases (including
 * pending host source bytes); destroy
 * packet before GPU shutdown and outside an open scene. Capture failure frees
 * all temporary packet resources but does not roll back evaluation/events.
 * Replay is declared in render_packet.h and never evaluates the model. */
EmRenderPacket *model_view_capture(float eye,float mouth);
/* Split capture: evaluate distinct explicit view/control/timeline triples in
 * parallel; freeze their inputs until all workers join. This phase has no GPU
 * or texture ownership operations. Finish each view on the serial render owner
 * at 960x544 before any command, step, reload, or context destruction. */
int model_view_evaluate(int width,int height,float eye,float mouth);
/* CPU-only geometry work for a frozen evaluated view. Partition once, run
 * disjoint half-open ranges with the same selected view, then join ALL ranges
 * before accepting stats or finishing a packet on the owner. No nested pool. */
enum { EM_VIEW_VERTEX_PARTS = 12 };
typedef struct {
 unsigned reused,rebuilt,changes[6];
 int64_t geometry_us,vertices_us,wall_us;
} EmViewVertexStats;
unsigned model_view_partition_vertices(unsigned cuts[EM_VIEW_VERTEX_PARTS+1]);
void model_view_run_vertices(unsigned first,unsigned last,int width,int height,EmViewVertexStats *out);
void model_view_accept_vertices(int width,int height,const EmViewVertexStats *stats);
EmRenderPacket *model_view_capture_evaluated(void);
const char *model_view_status(void);
/* Serial render-owner sample/reset: draws, eval_us, vertex_wall_us, submit_us,
 * sprites, rebuilt sprites. Geometry and worker waits remain included. */
void model_view_take_pose_profile(uint64_t out[6]);
/* Capture count, geometry, base-input changes, deformation-only, UV, color, reset. */
void model_view_take_dirty_profile(uint64_t out[7]);
void model_view_take_pose_parts(uint64_t out[10]);
/* Prepare owned host sources without pose capture; 1 ready, 0 deferred, -1 error. */
int model_view_prepare_host_textures(void);

#endif
