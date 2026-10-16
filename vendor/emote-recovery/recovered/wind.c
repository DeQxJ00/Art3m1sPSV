/* Reconstructed from the user's PCSG00341 eboot_dump ARM instructions.
 * No Art3m1s/FreeMote evaluation code is used in this implementation.
 */
#include "wind.h"

/* Original VA 0x8118A854. Padding/inactive particle values remain untouched. */
void em_wind_init(EmWind *w, float start, float end) {
    w->original_vtable_tag=0x81265FB4u;
    w->start=start; w->end=end; w->enabled=0;
    w->amplitude_a=1.0f; w->amplitude_b=0.0f;
    w->speed=0.0f; w->accumulator=0.0f;
    for(unsigned i=0;i<128;++i)w->particles[i].active=0;
}

/* Original VA 0x8118AA56. s0/s1/s2 are hard-float arguments, omitted by
 * the untyped Hex-Rays output; their use is confirmed in the ARM listing. */
void em_wind_configure(EmWind *w, float a, float b, float speed) {
    w->amplitude_a=a; w->amplitude_b=b;
    w->speed=((w->end-w->start)>=0.0f ? 1.0f : -1.0f)*speed;
    w->enabled=1; w->accumulator=0.0f;
}

/* The two inline PRNG sequences at 0x8118A90C and 0x8118A974. The original
 * gets a shared state via 0x8118A720; it is explicit here to allow comparison. */
static float random_unit(EmRandom *r) {
    uint32_t t=r->x^(r->x<<11);
    uint32_t next=t^(t>>8)^r->w^(r->w>>19);
    r->x=r->y; r->y=r->z; r->z=r->w; r->w=next;
    union { uint32_t bits; float value; } value;
    value.bits=(next&0x7fffffu)|0x3f800000u;
    return value.value-1.0f;
}

/* Original VA 0x8118A8D2. Deliberately preserve the >=0 boundary: even a
 * zero-frame call can perform a spawn trial when the accumulator is zero.
 * The original caller 0x8118AA4A separately checks the enabled flag.
 * Compile without fast-math or fused multiply-add contraction.
 */
void em_wind_step(EmWind *w, EmRandom *r, float frames) {
    w->accumulator+=frames;
    while(w->accumulator>=0.0f) {
        w->accumulator-=1.0f;
        if(random_unit(r)<0.0625f) {
            for(unsigned i=0;i<128;++i) {
                EmWindParticle *p=&w->particles[i];
                if(p->active)continue;
                p->active=1; p->position=w->start;
                float range=w->amplitude_a-w->amplitude_b;
                p->amplitude=range*random_unit(r)+w->amplitude_b;
                break;
            }
        }
    }
    for(unsigned i=0;i<128;++i) {
        EmWindParticle *p=&w->particles[i];
        if(!p->active)continue;
        p->position+=w->speed*frames;
        if((w->speed>0.0f && p->position>w->end) ||
           (w->speed<0.0f && p->position<w->end))p->active=0;
    }
}
