#include "physics_lookup.h"
static int expand(const EmPhysicsLookupNode *nodes,unsigned node_count,unsigned node,
                  unsigned *order,unsigned capacity,unsigned *used,unsigned depth){
 if(node>=node_count||depth>=node_count)return 0;
 const EmPhysicsLookupNode *n=nodes+node;
 unsigned start=*used;
 if(n->count>capacity-start||(!n->children&&n->count))return 0;
 for(unsigned i=0;i<n->count;++i){unsigned child=n->children[i];if(child>=node_count)return 0;order[(*used)++]=child;}
 unsigned end=*used;
 for(unsigned i=start;i<end;++i)if(!expand(nodes,node_count,order[i],order,capacity,used,depth+1))return 0;
 return 1;
}
unsigned em_physics_lookup_order(const EmPhysicsLookupNode *nodes,unsigned node_count,
                                unsigned root,unsigned *order,unsigned capacity){
 if(!nodes||!order||!capacity||root>=node_count)return 0;
 unsigned used=1;order[0]=root;
 return expand(nodes,node_count,root,order,capacity,&used,0)?used:0;
}
