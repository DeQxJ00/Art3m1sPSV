#ifndef EM_MESH_H
#define EM_MESH_H
typedef struct {float x,y;} EmPoint;
/* 8118AF86: evaluate 4x4 cubic patch, returned in S0/S1. */
EmPoint em_mesh_patch(const EmPoint *control,float u,float v);
typedef struct {EmPoint control[16];float inverse[4],offset[2];} EmWorldPatch;
/* 8119F190: inverse-map input world point, evaluate world control points. */
EmPoint em_mesh_world(const EmWorldPatch *patch,EmPoint point);
void em_mesh_world_many(const EmWorldPatch *patch,EmPoint *points,unsigned count);
/* Normalized samples, with the same per-point reduction order. */
void em_mesh_patch_many(const EmPoint control[16],EmPoint *points,unsigned count);
/* Render-only bounded reassociation; original APIs remain exact. */
/* A prepared verdict is valid only while the patch control points are
 * unchanged. Per-vertex inverse-map bounds and exact fallback remain live. */
int em_mesh_world_grid_bounded(const EmWorldPatch *patch);
void em_mesh_world_grid_prepared(const EmWorldPatch *patch,EmPoint *points,unsigned count,int bounded);
void em_mesh_world_grid(const EmWorldPatch *patch,EmPoint *points,unsigned count);
#endif
