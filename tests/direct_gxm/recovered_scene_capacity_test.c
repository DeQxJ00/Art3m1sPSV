#include <assert.h>
#include <string.h>
#include "scene_runtime.h"
int main(void){
 EmScene s={0};EmSceneMotion motions[3]={0};EmSceneLayer layers[2]={0};EmSceneFrame frames[2]={0};unsigned cap[9];
 s.motions=motions;s.motion_count=3;s.base_motion=0;
 motions[0].layer_count=1;motions[0].layers=layers;layers[0].frame_count=2;layers[0].frames=frames;
 frames[0].content.child_motion=1;frames[1].content.child_motion=2;
 EmSceneLayer large[900]={0};motions[1].layer_count=900;motions[1].layers=large;
 motions[2].layer_count=1;motions[2].layers=layers+1;
 assert(em_scene_node_capacities(&s,cap));assert(cap[0]==1&&cap[1]==900);for(int i=2;i<9;++i)assert(cap[i]==0);
 layers[1].frame_count=1;layers[1].frames=frames+1;
 assert(em_scene_node_capacities(&s,cap));for(int i=2;i<9;++i)assert(cap[i]==1);
 s.base_motion=3;assert(!em_scene_node_capacities(&s,cap));return 0;
}
