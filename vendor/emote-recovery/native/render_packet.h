#ifndef EM_RENDER_PACKET_H
#define EM_RENDER_PACKET_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
#define EM_RENDER_ASSERT static_assert
extern "C" {
#else
#define EM_RENDER_ASSERT _Static_assert
#endif
#define EM_RENDER_PACKET_VERSION 1u
#define EM_RENDER_PACKET_WIDTH 960u
#define EM_RENDER_PACKET_HEIGHT 544u
/* In-process owned snapshot, not a serialized wire format. Current adapter:
 * fixed 960x544 canvas, NDC XY, unchanged Z/W/RGBA/UV, uint16 triangle indices, at most81 vertices per mesh. */
typedef struct {float x,y,z,w,r,g,b,a,u,v;} EmRenderVertex;
EM_RENDER_ASSERT(sizeof(EmRenderVertex)==40,"packet vertex size");
EM_RENDER_ASSERT(offsetof(EmRenderVertex,r)==16&&offsetof(EmRenderVertex,u)==32,"packet vertex fields");
typedef enum {EM_RENDER_MATERIAL,EM_RENDER_MASK_STATE,EM_RENDER_HARD_RESET,EM_RENDER_HARD_STEP,
 EM_RENDER_SOFT_BEGIN,EM_RENDER_SOFT_REGION,EM_RENDER_SOFT_POSITIVE,EM_RENDER_SOFT_NEGATIVE,
 EM_RENDER_SOFT_END,EM_RENDER_SOFT_DISABLE,EM_RENDER_MESH} EmRenderOp;
typedef struct {uint32_t op;int32_t args[4];uint32_t texture,vertex_first,vertex_count,index_first,index_count;} EmRenderCommand;
typedef struct {uint64_t identity;uint32_t width,height;} EmRenderTextureInfo;
typedef struct EmRenderPacket EmRenderPacket;
/* Serial render-thread lease. object is owned independently of PSB/view. GPU
 * context must outlive all leases; release only outside an open GPU scene.
 * resolve_gpu is private to the demo GPU adapter, not a host texture-ID conversion.
 * Host leases may omit it and supply prepare_host instead. Host release owns
 * physical GPU retirement; all callbacks remain serial owner-thread calls. */
typedef struct {void *object;void (*retain)(void *);void (*release)(void *);void *(*resolve_gpu)(void *);EmRenderTextureInfo info;int (*prepare_host)(void *,uint64_t *);} EmRenderLease;
EmRenderPacket *em_render_packet_create(void);
void em_render_packet_destroy(EmRenderPacket *packet);
int em_render_packet_command(EmRenderPacket *,EmRenderOp,int32_t,int32_t,int32_t,int32_t);
/* Vertex bytes must have EmRenderVertex layout; copied without typed aliasing. */
int em_render_packet_mesh(EmRenderPacket *,const EmRenderLease *,const void *,uint32_t,const uint16_t *,uint32_t);
/* Capacity hint only; normal append bounds/validation remain authoritative. */
int em_render_packet_reserve(EmRenderPacket *,uint32_t commands,uint32_t vertices,uint32_t indices);
/* Seal prevents later mutation. Invalid/failed builders cannot be replayed. */
int em_render_packet_seal(EmRenderPacket *);
uint32_t em_render_packet_version(const EmRenderPacket *);
/* Immutable packet proof: all XYZW are finite and within +/-65536. */
int em_render_packet_affine_bounded(const EmRenderPacket *);
const EmRenderCommand *em_render_packet_commands(const EmRenderPacket *,uint32_t *count);
const EmRenderVertex *em_render_packet_vertices(const EmRenderPacket *,uint32_t *count);
const uint16_t *em_render_packet_indices(const EmRenderPacket *,uint32_t *count);
int em_render_packet_texture(const EmRenderPacket *,uint32_t,EmRenderTextureInfo *);
uint32_t em_render_packet_texture_count(const EmRenderPacket *);
/* Serial, outside host scene. 1 all ready, 0 retry, -1 error. Outputs are
 * zero on retry/error; no evaluation and no partial draw is permitted. */
int em_render_packet_prepare_host(const EmRenderPacket *,uint64_t *handles,uint32_t capacity);
/* Requires current adapter's live gpu_begin scene. No begin/end/evaluation or
 * clock/event processing. On failure returns first error, attempts mask disable,
 * but cannot undo pixels or promise restoration of arbitrary host GPU state. */
int em_render_packet_replay(const EmRenderPacket *);
#ifdef EM_RENDER_PACKET_TEST
void em_render_packet_test_fail_after(long successful_allocations);
#endif
#ifdef __cplusplus
}
#endif
#undef EM_RENDER_ASSERT
#endif
