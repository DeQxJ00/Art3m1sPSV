#ifndef EM_MATERIAL_COLORS_H
#define EM_MATERIAL_COLORS_H
#include <stdint.h>
/* 8118C684 integer grid interpolation. Input corners are TL,TR,BL,BR;
 * original packet/PSB clockwise order must first be reordered 0,1,3,2.
 * Output is row-major (columns+1)*(rows+1), in the same packed byte order.
 * Each axis uses floor(256*index/subdivisions); interpolate Y edges, truncate,
 * then interpolate X and truncate again. No float rounding or premultiply.
 * Returns 1 on success, 0 on invalid dimensions/capacity without writing. */
int em_material_color_grid(const uint32_t corners[4],unsigned columns,
                           unsigned rows,uint32_t *out,unsigned capacity);
/* 811A117C..811A1240: apply effective layer opacity to the raw corner's
 * alpha FIRST, then motion-global tint. RGB multiply is /128 and saturated;
 * both alpha multiplications separately truncate /255. */
uint32_t em_material_prepare_color(uint32_t rgba,uint8_t opacity,uint32_t tint);
#endif
