/* PCSG00341 original E-mote EPWindControl layout, recovered from ARM code.
 * Evidence: reverse/PCSG00341/functions/8118A854.asm and 8118A8D2.asm.
 * Field names are descriptive reconstructions, not original source symbols.
 */
#ifndef RECOVERED_WIND_H
#define RECOVERED_WIND_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    uint8_t active, padding[3];
    float position, amplitude;
} EmWindParticle;
typedef struct {
    uint32_t original_vtable_tag;
    float start, end;
    uint8_t enabled, padding[3];
    float amplitude_a, amplitude_b, speed, accumulator;
    EmWindParticle particles[128];
} EmWind;
typedef struct { uint32_t x, y, z, w; } EmRandom;
_Static_assert(sizeof(EmWindParticle)==12, "original particle stride");
_Static_assert(offsetof(EmWind, particles)==32, "original particle offset");
_Static_assert(sizeof(EmWind)==1568, "original object size");
void em_wind_init(EmWind *wind, float start, float end);
void em_wind_configure(EmWind *wind, float amplitude_a, float amplitude_b, float speed);
void em_wind_step(EmWind *wind, EmRandom *random, float frames);
#endif
