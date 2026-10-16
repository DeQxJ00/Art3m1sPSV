#ifndef EM_MESH_CHILD_H
#define EM_MESH_CHILD_H
#include "mesh.h"
/* 811A0106..811A01C2, after caller checks parent's bit1/type/active patch.
 * Point is CHILD LOCAL translation, before parent affine. offset is parent's
 * rect origin + current content origin; coordinate selects y versus z outside.
 * No dimension-zero guard is present in the original. */
EmPoint em_mesh_child_position(const EmPoint patch[16],EmPoint point,
                              int width,int height,EmPoint offset);
/* Import-boundary helpers allow strict original-instruction comparison without
 * substituting Sony atan2f/fabsf in the original program. L/R/B/T order. */
void em_mesh_child_samples(const EmPoint patch[16],float u,float v,EmPoint samples[4]);
void em_mesh_child_angle_args(const EmPoint samples[4],float args[4]);
float em_mesh_child_angle_add(float angle,float horizontal_atan,float vertical_atan);
void em_mesh_child_areas(const EmPoint samples[4],float signed_areas[2]);
float em_mesh_child_scale(float absolute_area0,float absolute_area1);
typedef struct {EmPoint position;float angle,scale_x,scale_y;} EmMeshChildPose;
/* Caller checks the original parent type/visibility/nonempty-patch guards.
 * bits2/4 require bit1 and child's inherit angle/scale bits, just as original.
 * Uses C libm atan2f, not a claim of Sony transcendental last-bit parity. */
void em_mesh_child_sync(EmMeshChildPose *pose,const EmPoint patch[16],int width,int height,
                        EmPoint offset,unsigned sync_mask,unsigned inherit_mask);
#endif
