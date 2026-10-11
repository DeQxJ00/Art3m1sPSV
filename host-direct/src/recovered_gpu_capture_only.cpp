// Recovery evaluator is linked for packet capture only. Legacy demo GPU calls
// must fail closed; the host GPU remains the sole context/device owner.
#include "gpu.hpp"
extern "C" {
#include "gpu.h"
static int unavailable(){return -1;}
int gpu_init(){return unavailable();}
void gpu_shutdown(){}
void gpu_wait(){direct::wait();}
int gpu_texture(GpuTexture*,int,int,const uint32_t*){return unavailable();}
int gpu_texture_bc3_linear(GpuTexture*,int,int,const void*,unsigned){return unavailable();}
int gpu_texture_bc3_swizzled(GpuTexture*,int,int,const void*,unsigned){return unavailable();}
void gpu_texture_free(GpuTexture* texture){
    // The host path never fills this legacy object. Report contract violations.
    if(texture&&texture->memory.data)direct::log("[recovery-error] legacy texture escaped host-only capture path");
}
uint32_t* gpu_ui_pixels(){return nullptr;}
int gpu_begin(){return unavailable();}
int gpu_mesh(GpuTexture*,const GpuVertex*,int,const uint16_t*,int){return unavailable();}
int gpu_grid(GpuTexture*,const GpuVertex*,int){return unavailable();}
int gpu_material_mode(unsigned){return unavailable();}
int gpu_mask_state(unsigned,unsigned){return unavailable();}
int gpu_mask_clear(){return unavailable();}
int gpu_hard_mask_reset(unsigned){return unavailable();}
int gpu_hard_mask_step(unsigned,unsigned){return unavailable();}
int gpu_soft_mask_begin(){return unavailable();}
int gpu_soft_mask_region(int,int,int,int){return unavailable();}
int gpu_soft_mask_positive(){return unavailable();}
int gpu_soft_mask_negative(){return unavailable();}
int gpu_soft_mask_end(){return unavailable();}
int gpu_soft_mask_disable(){return unavailable();}
int gpu_end(){return unavailable();}
void gpu_capture_next(){}
}
