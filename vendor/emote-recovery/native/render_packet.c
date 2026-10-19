#include "render_packet.h"
#include "render_packet_internal.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#ifdef EM_RENDER_PACKET_TEST
static long allocation_budget=-1;
void em_render_packet_test_fail_after(long n){allocation_budget=n;}
static int allocation_allowed(void){if(allocation_budget<0)return 1;if(!allocation_budget)return 0;--allocation_budget;return 1;}
#else
static int allocation_allowed(void){return 1;}
#endif
static int reserve(void **data,uint32_t *capacity,uint32_t need,size_t size){
 if(need<=*capacity)return 1;uint32_t n=*capacity?*capacity:8;
 while(n<need){if(n>UINT32_MAX/2){n=need;break;}n*=2;}
 if(n>SIZE_MAX/size||!allocation_allowed())return 0;void *p=realloc(*data,(size_t)n*size);if(!p)return 0;*data=p;*capacity=n;return 1;
}
EmRenderPacket *em_render_packet_create(void){return allocation_allowed()?calloc(1,sizeof(EmRenderPacket)):NULL;}
void em_render_packet_destroy(EmRenderPacket*p){if(!p)return;for(uint32_t i=0;i<p->lease_count;++i)p->leases[i].release(p->leases[i].object);free(p->leases);free(p->commands);free(p->vertices);free(p->indices);free(p);}
static int fail(EmRenderPacket*p){if(p&&!p->sealed)p->failed=1;return -1;}
int em_render_packet_command(EmRenderPacket*p,EmRenderOp op,int32_t a,int32_t b,int32_t c,int32_t d){
 if(!p||p->failed||p->sealed||(uint32_t)op>=EM_RENDER_MESH)return fail(p);
 if((op==EM_RENDER_MATERIAL&&(uint32_t)a!=UINT32_MAX&&!((uint32_t)a<=5||((uint32_t)a>=16&&(uint32_t)a<=21)))||
    (op==EM_RENDER_MASK_STATE&&(((uint32_t)a|(uint32_t)b)>255))||
    (op==EM_RENDER_HARD_RESET&&(uint32_t)a>1)||
    (op==EM_RENDER_HARD_STEP&&((uint32_t)a>127||(uint32_t)b>2)))return fail(p);
 if(p->command_count==UINT32_MAX||!reserve((void**)&p->commands,&p->command_capacity,p->command_count+1,sizeof(*p->commands)))return fail(p);
 p->commands[p->command_count++]=(EmRenderCommand){.op=op,.args={a,b,c,d}};return 0;
}
int em_render_packet_reserve(EmRenderPacket*p,uint32_t commands,uint32_t vertices,uint32_t indices){
 if(!p||p->failed||p->sealed)return fail(p);
 if(!reserve((void**)&p->commands,&p->command_capacity,commands,sizeof(*p->commands))||
    !reserve((void**)&p->vertices,&p->vertex_capacity,vertices,sizeof(*p->vertices))||
    !reserve((void**)&p->indices,&p->index_capacity,indices,sizeof(*p->indices)))return fail(p);
 return 0;
}
/* Derive the affine input bound while copying the immutable vertex stream.
 * No second scan of the completed packet on the serial submission path.
 * Integer absolute-bit comparisons reject NaN/Inf without FP exceptions. */
static unsigned copy_vertices(EmRenderVertex *dst,const void *source,uint32_t count){
 unsigned bad=0;const unsigned char *src=source;
 /* Keep the platform bulk copy: short interleaved NEON stores are slower on
  * the device. Inspect position lanes separately in the cached source. */
 memcpy(dst,source,(size_t)count*sizeof(*dst));
#if defined(__ARM_NEON)
 uint32x4_t invalid=vdupq_n_u32(0),mask=vdupq_n_u32(0x7fffffffu),limit=vdupq_n_u32(0x47800000u);
 for(uint32_t i=0;i<count;++i,src+=40){
  uint8x16_t pos=vld1q_u8(src);invalid=vorrq_u32(invalid,vcgtq_u32(vandq_u32(vreinterpretq_u32_u8(pos),mask),limit));
 }
 uint32_t lanes[4];vst1q_u32(lanes,invalid);bad=lanes[0]|lanes[1]|lanes[2]|lanes[3];
#else
 for(uint32_t i=0;i<count;++i){uint32_t bits[4];memcpy(bits,src+i*40,sizeof bits);
  for(unsigned k=0;k<4;++k)bad|=(bits[k]&0x7fffffffu)>0x47800000u;
 }
#endif
 return !!bad;
}
int em_render_packet_mesh(EmRenderPacket*p,const EmRenderLease*l,const void*v,uint32_t n,const uint16_t*ix,uint32_t ni){
 if(!p||p->failed||p->sealed||!l||!l->object||!l->retain||!l->release||(!l->resolve_gpu&&!l->prepare_host)||!l->info.identity||!l->info.width||!l->info.height||!v||!ix||!n||!ni||ni%3||n>81||ni>INT_MAX)return fail(p);
 for(uint32_t i=0;i<ni;++i)if(ix[i]>=n)return fail(p);
 uint32_t resource=0;for(;resource<p->lease_count;++resource)if(p->leases[resource].object==l->object)break;
 if(resource<p->lease_count&&(p->leases[resource].retain!=l->retain||p->leases[resource].release!=l->release||p->leases[resource].resolve_gpu!=l->resolve_gpu||p->leases[resource].prepare_host!=l->prepare_host||p->leases[resource].info.identity!=l->info.identity||p->leases[resource].info.width!=l->info.width||p->leases[resource].info.height!=l->info.height))return fail(p);
 if(p->command_count==UINT32_MAX||p->vertex_count>UINT32_MAX-n||p->index_count>UINT32_MAX-ni||p->lease_count==UINT32_MAX)return fail(p);
 if(!reserve((void**)&p->commands,&p->command_capacity,p->command_count+1,sizeof(*p->commands))||
    !reserve((void**)&p->vertices,&p->vertex_capacity,p->vertex_count+n,sizeof(*p->vertices))||
    !reserve((void**)&p->indices,&p->index_capacity,p->index_count+ni,sizeof(*p->indices))||
    (resource==p->lease_count&&!reserve((void**)&p->leases,&p->lease_capacity,p->lease_count+1,sizeof(*p->leases))))return fail(p);
 if(resource==p->lease_count){p->leases[p->lease_count++]=*l;l->retain(l->object);}
 p->positions_unbounded|=copy_vertices(p->vertices+p->vertex_count,v,n);memcpy(p->indices+p->index_count,ix,(size_t)ni*sizeof(*ix));
 p->commands[p->command_count++]=(EmRenderCommand){.op=EM_RENDER_MESH,.texture=resource,.vertex_first=p->vertex_count,.vertex_count=n,.index_first=p->index_count,.index_count=ni};p->vertex_count+=n;p->index_count+=ni;return 0;
}
int em_render_packet_seal(EmRenderPacket*p){if(!p||p->failed)return 0;p->sealed=1;return 1;}
int em_render_packet_affine_bounded(const EmRenderPacket*p){return em_render_packet_version(p)&&!p->positions_unbounded;}
uint32_t em_render_packet_version(const EmRenderPacket*p){return p&&p->sealed&&!p->failed?EM_RENDER_PACKET_VERSION:0;}
const EmRenderCommand *em_render_packet_commands(const EmRenderPacket*p,uint32_t*n){if(n)*n=em_render_packet_version(p)?p->command_count:0;return em_render_packet_version(p)?p->commands:NULL;}
const EmRenderVertex *em_render_packet_vertices(const EmRenderPacket*p,uint32_t*n){if(n)*n=em_render_packet_version(p)?p->vertex_count:0;return em_render_packet_version(p)?p->vertices:NULL;}
const uint16_t *em_render_packet_indices(const EmRenderPacket*p,uint32_t*n){if(n)*n=em_render_packet_version(p)?p->index_count:0;return em_render_packet_version(p)?p->indices:NULL;}
int em_render_packet_texture(const EmRenderPacket*p,uint32_t i,EmRenderTextureInfo*out){if(!em_render_packet_version(p)||i>=p->lease_count||!out)return 0;*out=p->leases[i].info;return 1;}
uint32_t em_render_packet_texture_count(const EmRenderPacket*p){return em_render_packet_version(p)?p->lease_count:0;}

int em_render_packet_prepare_host(const EmRenderPacket*p,uint64_t*handles,uint32_t capacity){
 if(!em_render_packet_version(p)||capacity<p->lease_count||(p->lease_count&&!handles))return -1;
 int result=1;
 for(uint32_t i=0;i<p->lease_count;++i){uint64_t h=0;const EmRenderLease*l=&p->leases[i];int r=l->prepare_host?l->prepare_host(l->object,&h):-1;
  if(r<0||(r==1&&!h)||r>1||(r==0&&h))result=-1;else if(!r&&result>0)result=0;handles[i]=h;
 }
 if(result!=1&&p->lease_count)memset(handles,0,(size_t)p->lease_count*sizeof(*handles));return result;
}
