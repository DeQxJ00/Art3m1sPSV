#ifndef EM_MESH_H
#define EM_MESH_H
typedef struct {float x,y;} EmPoint;
/* 8118AF86: evaluate 4x4 cubic patch, returned in S0/S1. */
EmPoint em_mesh_patch(const EmPoint *control,float u,float v);
typedef struct {EmPoint control[16];float inverse[4],offset[2];} EmWorldPatch;
/* 8119F190: inverse-map input world point, evaluate world control points. */
EmPoint em_mesh_world(const EmWorldPatch *patch,EmPoint point);
void em_mesh_world_many(const EmWorldPatch *patch,EmPoint *points,unsigned count);
#endif
