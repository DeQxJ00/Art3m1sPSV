#ifndef EM_RENDER_PACKET_INTERNAL_H
#define EM_RENDER_PACKET_INTERNAL_H
#include "render_packet.h"
struct EmRenderPacket {EmRenderCommand *commands;EmRenderVertex *vertices;uint16_t *indices;EmRenderLease *leases;
 uint32_t command_count,vertex_count,index_count,lease_count,command_capacity,vertex_capacity,index_capacity,lease_capacity;int failed,sealed;unsigned positions_unbounded;int profile;uint32_t topology_first[5],topology_ready;};
#endif
