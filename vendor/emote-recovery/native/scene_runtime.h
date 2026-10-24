#ifndef EM_SCENE_RUNTIME_H
#define EM_SCENE_RUNTIME_H
#include "psb.h"
#include "parameter.h"
#include "mesh.h"
#include "curve.h"
#include "motion_events.h"

/* Binary-derived fields, deliberately not a claim of original ABI/layout.
 * 81190FFE binds layer parameters and static flags; 8119AC18/81199F7E
 * decode frame contents. We predecode the currently supported fields once.
 */
typedef struct {
    float coord[3],zx,zy,angle,opa,shear_x,shear_y;
    uint32_t colors[4];
    int fx,fy,has_patch;
    EmPoint cp[16];
    float content_origin_x,content_origin_y; /* raw frame ox/oy, additive for mesh sync */
    float width,height,origin_x,origin_y; /* mesh rectangle, including blank */
    int atlas,icon_valid,skip_sprite,child_motion;
    float child_time_offset;
    float left,top,w,h;
    int attr;
    float ox,oy; /* effective sprite origin after content-mask override */
    int curves[5]; /* coordinate, packed color, angle, zoom, shear; -1 absent */
    EmPath *path;
    unsigned mask;
    int blend_mode,time_interval;
} EmSceneContent;
typedef struct {float time;int type;EmSceneContent content;} EmSceneFrame;
/* sample01 DLL 180058BB0: raw meshType=1, 16 XY float pairs per sample. */
typedef struct {const char *id;float begin,end;unsigned count;int neutral;EmPoint *patches;} EmSceneCombinator;
typedef struct {
    EmCurve curve;
    unsigned block_count;
} EmSceneCurve;
typedef struct {
    const char *label;
    int parent,type,parameter,has_parameter,mesh_transform,mesh_combine,coordinate,shape;
    unsigned inherit_mask,frame_count;
    unsigned mesh_sync_child_mask;
    int transform_order[4];
    int stencil_type;
    int motion_independent_layer_inherit; /* reference layer -> child model+756 */
    unsigned mask_count;
    int *mask_layers;
    EmSceneFrame *frames;
    EmMotionEventFrame *event_frames; /* present only for authored act keys */
    unsigned combinator_count;
    EmSceneCombinator *combinators;
} EmSceneLayer;
typedef struct {const char *id;EmParameter range;float frame;unsigned epoch;} EmSceneParameter;
typedef struct {float time;unsigned count;int *indices;} EmScenePriority;
typedef struct {
    EmPsbValue source; /* construction/reference resolution only */
    int parameter,has_parameter;
    float length,loop_begin,loop_end,loop_time; /* loopTime: original motion clock */
    unsigned layer_count,parameter_count,priority_count;
    unsigned pose_offset; /* immutable index into per-view update state */
    EmSceneLayer *layers;
    EmSceneParameter *parameters;
    EmScenePriority *priorities;
    unsigned tag_count;
    EmMotionEventFrame *tags;
} EmSceneMotion;
typedef struct {
    EmSceneMotion *motions;
    unsigned motion_count;
    int base_motion;
    EmSceneFrame empty_frame;
    unsigned total_layers,total_frames,total_event_frames;
    size_t bytes;
    size_t allocation_limit; /* zero means foreground/unlimited */
    EmSceneCurve *curves;
    unsigned curve_count;
    int borrowed_inputs; /* host clone owns motions/parameters only; prototype must outlive it */
} EmScene;
float em_scene_curve(const EmScene *scene,int index,float ratio);
/* Last empty key is a sentinel, not a pose (8119ADC0 / 8119C06A).
 * The caller handles an empty frame list. Stateless lookup also supports seek. */
unsigned em_scene_frame_index(const EmSceneLayer *layer,float time);
int em_scene_compile_limited(EmScene *scene,EmPsbValue root,const char *const *atlases,unsigned atlas_count,size_t limit);
int em_scene_node_capacities(const EmScene *scene,unsigned capacities[9]);
int em_scene_compile(EmScene *scene,EmPsbValue root,const char *const *atlases,unsigned atlas_count);
int em_scene_clone(EmScene *scene,const EmScene *prototype);
void em_scene_free(EmScene *scene);
#endif
