#include "mesh_child.h"
#include <math.h>
EmPoint em_mesh_child_position(const EmPoint patch[16],EmPoint point,
                              int width,int height,EmPoint offset){
    float u=(point.x+offset.x)/(float)width;
    float v=(point.y+offset.y)/(float)height;
    EmPoint warped=em_mesh_patch(patch,u,v);
    return (EmPoint){warped.x*(float)width-offset.x,warped.y*(float)height-offset.y};
}

void em_mesh_child_samples(const EmPoint patch[16],float u,float v,EmPoint samples[4]){
    /* 811A01EC..811A02A4: keep explicit +/-0.0 operands and sample ordering. */
    const float delta=0x1.a36e2ep-14f; /* 0x38D1B717 */
    samples[0]=(EmPoint){u-delta,v+0.0f};
    samples[1]=(EmPoint){u+delta,v+0.0f};
    samples[2]=(EmPoint){u+0.0f,v-delta};
    samples[3]=(EmPoint){u+0.0f,v+delta};
    em_mesh_patch_many(patch,samples,4);
}
void em_mesh_child_angle_args(const EmPoint s[4],float args[4]){
    args[0]=s[1].y-s[0].y;args[1]=s[1].x-s[0].x;
    args[2]=-(s[3].x-s[2].x);args[3]=s[3].y-s[2].y;
}
float em_mesh_child_angle_add(float angle,float horizontal_atan,float vertical_atan){
    return angle+(((horizontal_atan+vertical_atan)*.5f)*360.0f)/0x1.921fb6p+2f;
}
void em_mesh_child_areas(const EmPoint s[4],float area[2]){
    float dy=s[1].y-s[0].y,dx=s[1].x-s[0].x;
    area[0]=dx*(s[2].y-s[0].y)-dy*(s[2].x-s[0].x);
    area[1]=dx*(s[3].y-s[0].y)-dy*(s[3].x-s[0].x);
}
float em_mesh_child_scale(float area0,float area1){
    float sum=area1*.5f+area0*.5f;
    return sqrtf(sum*2.0f)/0x1.a36e2ep-13f; /* 0x3951B717 */
}
void em_mesh_child_sync(EmMeshChildPose *pose,const EmPoint patch[16],int width,int height,
                        EmPoint offset,unsigned sync_mask,unsigned inherit_mask){
    if(!(sync_mask&1))return;
    float u=(pose->position.x+offset.x)/(float)width,v=(pose->position.y+offset.y)/(float)height;
    EmPoint warped=em_mesh_patch(patch,u,v);
    pose->position=(EmPoint){warped.x*(float)width-offset.x,warped.y*(float)height-offset.y};
    int angle=(sync_mask&2)&&(inherit_mask&0x10),scale=(sync_mask&4)&&(inherit_mask&0x60);
    if(angle||scale){EmPoint s[4];em_mesh_child_samples(patch,u,v,s);
        if(angle){float args[4];em_mesh_child_angle_args(s,args);
            pose->angle=em_mesh_child_angle_add(pose->angle,atan2f(args[0],args[1]),atan2f(args[2],args[3]));}
        if(scale){float areas[2];em_mesh_child_areas(s,areas);float ratio=em_mesh_child_scale(fabsf(areas[0]),fabsf(areas[1]));
            if(inherit_mask&0x20)pose->scale_x=pose->scale_x*ratio;
            if(inherit_mask&0x40)pose->scale_y=pose->scale_y*ratio;
        }
    }
}
