#ifndef EM_ADAPTER_MATERIAL_H
#define EM_ADAPTER_MATERIAL_H
#include <stdint.h>
/* Adapter vertices already carry the doubled (0x80-neutral) tint. Select
 * the non-doubling shader family while retaining the authored blend equation.
 * Ordinary/unknown modes keep the established adapter path. */
static inline unsigned em_adapter_material(unsigned authored,int original){
 if(original)return authored;
 unsigned low=authored&15;
 return !(authored&~31u)&&low>=1&&low<=5?low:UINT32_MAX;
}
static inline float em_adapter_color_divisor(unsigned authored){
 return em_adapter_material(authored,0)!=UINT32_MAX&&!(authored&16)?255.f:128.f;
}
#endif
