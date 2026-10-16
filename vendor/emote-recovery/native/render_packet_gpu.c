/* Current demo adapter only; the packet core does not depend on Sony headers. */
#include "render_packet_internal.h"
#include "gpu.h"
#include <string.h>
_Static_assert(sizeof(GpuVertex)==sizeof(EmRenderVertex),"GPU vertex ABI");
_Static_assert(offsetof(GpuVertex,x)==offsetof(EmRenderVertex,x)&&offsetof(GpuVertex,r)==offsetof(EmRenderVertex,r)&&offsetof(GpuVertex,u)==offsetof(EmRenderVertex,u),"GPU vertex fields");
int em_render_packet_replay(const EmRenderPacket*p){
 if(!p||p->failed||!p->sealed)return -1;
 for(uint32_t i=0;i<p->lease_count;++i)if(!p->leases[i].resolve_gpu||!p->leases[i].resolve_gpu(p->leases[i].object))return -1;
 for(uint32_t i=0;i<p->command_count;++i){const EmRenderCommand*c=p->commands+i;int rc=-1;
  switch(c->op){
   case EM_RENDER_MATERIAL:rc=gpu_material_mode((uint32_t)c->args[0]);break;
   case EM_RENDER_MASK_STATE:rc=gpu_mask_state(c->args[0],c->args[1]);break;
   case EM_RENDER_HARD_RESET:rc=gpu_hard_mask_reset(c->args[0]);break;
   case EM_RENDER_HARD_STEP:rc=gpu_hard_mask_step(c->args[0],c->args[1]);break;
   case EM_RENDER_SOFT_BEGIN:rc=gpu_soft_mask_begin();break;
   case EM_RENDER_SOFT_REGION:rc=gpu_soft_mask_region(c->args[0],c->args[1],c->args[2],c->args[3]);break;
   case EM_RENDER_SOFT_POSITIVE:rc=gpu_soft_mask_positive();break;
   case EM_RENDER_SOFT_NEGATIVE:rc=gpu_soft_mask_negative();break;
   case EM_RENDER_SOFT_END:rc=gpu_soft_mask_end();break;
   case EM_RENDER_SOFT_DISABLE:rc=gpu_soft_mask_disable();break;
   case EM_RENDER_MESH:{GpuTexture*t=p->leases[c->texture].resolve_gpu(p->leases[c->texture].object);
    /* Copy avoids aliasing distinct C struct types at the GPU API boundary. */
    GpuVertex v[81];memcpy(v,p->vertices+c->vertex_first,c->vertex_count*sizeof(*v));
    rc=t?gpu_mesh(t,v,(int)c->vertex_count,p->indices+c->index_first,(int)c->index_count):-1;break;}
  }
  if(rc<0){gpu_soft_mask_disable();gpu_mask_state(0,0);return rc;}
 }return 0;
}
