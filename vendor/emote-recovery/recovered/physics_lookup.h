#ifndef RECOVERED_PHYSICS_LOOKUP_H
#define RECOVERED_PHYSICS_LOOKUP_H
/* Nodes are evaluated child INSTANCES, not shared motion-definition indices.
 * Children must list all direct type-3 references in authored layer order,
 * followed by particle child instances in their original list order. Empty
 * unloaded instances may be omitted when they have no layers or descendants. */
typedef struct {const unsigned *children;unsigned count;} EmPhysicsLookupNode;
/* Original 8119732A/81190DDA: append all siblings, then expand each appended
 * sibling recursively. This is neither DFS nor global breadth-first order.
 * Returns count including root, or zero for malformed graph/capacity failure.
 * On failure discard partial output. Bounds/cycle checks are adapter safety. */
unsigned em_physics_lookup_order(const EmPhysicsLookupNode *nodes,unsigned node_count,
                                unsigned root,unsigned *order,unsigned capacity);
#endif
