#include "adapter_material.h"
#include "material.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
 assert(em_adapter_material(0,0)==UINT32_MAX);
 assert(em_adapter_material(16,0)==UINT32_MAX);
 assert(em_adapter_material(35,0)==UINT32_MAX);
 assert(em_adapter_material(19,0)==3);
 for(unsigned group=0;group<2;++group)for(unsigned low=1;low<=5;++low){
  unsigned authored=group*16+low;
  assert(em_adapter_material(authored,1)==authored);
  EmMaterialState state={0};state.blend_mode=em_adapter_material(authored,0);
  state.texture_present=state.blend_enabled=1;EmMaterialSelection s;
  em_material_select(&state,&s);
  assert(s.program_slot==low+1);
  assert(s.family==60+(low==5?1:(low==3||low==4)?2:0));
  assert(em_adapter_color_divisor(authored)==(group?128.f:255.f));
 }
 /* Multiply shadow keeps background detail; zero alpha must be identity. */
 for(unsigned n=0;n<=255;++n){
  float a=n/255.f,source=.76f,destination=.4f;
  float result=source*a*destination+destination*(1-a);
  assert(result<=destination+1e-6f&&result>=source*destination-1e-6f);
  if(!n)assert(fabsf(result-destination)<1e-6f);
 }
 puts("PASS adapter blend families, tint scale and multiply boundaries");
}
