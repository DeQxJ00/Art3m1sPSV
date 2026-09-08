#include <stdint.h>

/*
 * Rust's Vita build-std archive supplies a weak fmod that uses vqsub.u64.
 * Vita3K cannot currently translate that instruction and Yoga calls fmod for
 * every layout pass. Keep a strong scalar implementation in the host so both
 * Yoga and Rust use an instruction set supported by Vita3K and real Vita.
 */
double fmod(double value, double divisor) {
    union { double number; uint64_t bits; } x = {value}, y = {divisor};
    const uint64_t sign = x.bits & (UINT64_C(1) << 63);
    const uint64_t hidden = UINT64_C(1) << 52;
    const uint64_t infinity = UINT64_C(0x7ff0000000000000);
    x.bits &= UINT64_C(0x7fffffffffffffff);
    y.bits &= UINT64_C(0x7fffffffffffffff);
    if (x.bits > infinity || y.bits > infinity) return value + divisor;
    if (x.bits == infinity || y.bits == 0) return (value - value) / (value - value);
    if (x.bits < y.bits) return value;
    if (x.bits == y.bits) { x.bits = sign; return x.number; }

    int xe = (int)(x.bits >> 52), ye = (int)(y.bits >> 52);
    uint64_t remainder = x.bits & (hidden - 1);
    uint64_t denominator = y.bits & (hidden - 1);
    if (xe) remainder |= hidden;
    else {
        xe = 1;
        while (remainder < hidden) { remainder <<= 1; --xe; }
    }
    if (ye) denominator |= hidden;
    else {
        ye = 1;
        while (denominator < hidden) { denominator <<= 1; --ye; }
    }
    /* Binary long division: bounded by the binary64 exponent range, with
     * exact integer subtraction even when value/divisor would overflow. */
    while (xe > ye) {
        if (remainder >= denominator) remainder -= denominator;
        remainder <<= 1;
        --xe;
    }
    if (remainder >= denominator) remainder -= denominator;
    if (!remainder) { x.bits = sign; return x.number; }
    while (remainder < hidden) { remainder <<= 1; --xe; }
    if (xe > 0) x.bits = sign | ((uint64_t)xe << 52) | (remainder - hidden);
    else x.bits = sign | (remainder >> (1 - xe));
    return x.number;
}
