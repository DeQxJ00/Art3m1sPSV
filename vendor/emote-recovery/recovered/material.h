#ifndef EM_MATERIAL_H
#define EM_MATERIAL_H
#include <stdint.h>
/* Inputs are the renderer's prepared state, not raw PSB fields. Mapping PSB
 * bm/texture flags into that state is a separate recovery task. */
typedef struct {
 uint32_t blend_mode,texture_present,texture_flags,auxiliary_texture;
 int32_t filter;
 uint32_t blend_enabled;
} EmMaterialState;
typedef struct {uint32_t family,program_slot;} EmMaterialSelection;
/* 811A918A normal textured/colored fragment-program selection path.
 * Caller handles texturing-disabled and global override paths separately. */
void em_material_select(const EmMaterialState *state,EmMaterialSelection *out);
/* Complete fragment selection gate from 811A929A..811A93E6. The inputs
 * texturing_enabled (state+41) and override_active (renderer+973) are byte
 * flags. The disabled path skips material uniforms; override uses family 0
 * and slot 0 but still updates material uniforms. Neither skips vertex or
 * shared final uniforms. This is a plan, not a GPU command. */
typedef struct {
 uint32_t family,program_slot,update_material_uniforms;
} EmMaterialPlan;
void em_material_plan(const EmMaterialState *state,uint32_t texturing_enabled,
                      uint32_t override_active,EmMaterialPlan *out);
/* 811A8C84: exact four-byte SceGxmBlendInfo records, indexed 0..6.
 * Program slot zero uses NULL, slot n=1..6 uses record n (NOT n-1).
 * 811A94FE increments the record pointer even for the NULL slot zero. */
extern const uint8_t em_material_blend_states[7][4];
/* NULL for slot zero or unsupported slots beyond the normal 0..6 range. */
const uint8_t *em_material_blend_record(unsigned program_slot);
#endif
