#include "material.h"
const uint8_t em_material_blend_states[7][4]={
 {0x0f,0x00,0x01,0x10},{0x0f,0x11,0x54,0x51},
 {0x0f,0x11,0x14,0x10},{0x0f,0x13,0x14,0x10},
 {0x0f,0x11,0x56,0x10},{0x0f,0x11,0x17,0x10},
 {0x0f,0x13,0x14,0x10}
};
void em_material_select(const EmMaterialState *s,EmMaterialSelection *out){
 uint32_t low=s->blend_mode&15,texture=0;
 if(s->texture_present)texture=(s->texture_flags&4)?1:(s->blend_mode&16)?3:2;
 uint32_t variant=(s->auxiliary_texture!=0)|(texture<<1);
 uint32_t blend_family=low==5?1:(low==3||low==4)?2:0;
 uint32_t filter=s->filter>=1&&s->filter<=4?(uint32_t)s->filter:0;
 out->family=15*variant+3*filter+blend_family;
 out->program_slot=s->blend_enabled&&s->texture_flags!=2?(low>=1&&low<=5?low+1:1):0;
}

void em_material_plan(const EmMaterialState *s,uint32_t texturing_enabled,
                      uint32_t override_active,EmMaterialPlan *out){
 out->family=0;out->program_slot=0;
 out->update_material_uniforms=(texturing_enabled&255)!=0;
 if(out->update_material_uniforms&&!(override_active&255)){
  EmMaterialSelection selection;
  em_material_select(s,&selection);
  out->family=selection.family;out->program_slot=selection.program_slot;
 }
}

const uint8_t *em_material_blend_record(unsigned program_slot){
 return program_slot>=1&&program_slot<=6?em_material_blend_states[program_slot]:(const uint8_t*)0;
}
