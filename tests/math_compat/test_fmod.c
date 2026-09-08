#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

double art3m1s_test_fmod(double, double);
typedef union { double number; uint64_t bits; } Binary64;

static void check(Binary64 x, Binary64 y) {
    Binary64 expected = {.number = fmod(x.number, y.number)};
    Binary64 actual = {.number = art3m1s_test_fmod(x.number, y.number)};
    if ((isnan(expected.number) && isnan(actual.number)) || expected.bits == actual.bits) return;
    fprintf(stderr, "fmod(%a, %a): expected %a [%016llx], got %a [%016llx]\n",
        x.number, y.number, expected.number, (unsigned long long)expected.bits,
        actual.number, (unsigned long long)actual.bits);
    exit(1);
}

static uint64_t random_bits(void) {
    static uint64_t state = UINT64_C(0x83579acdf0246811);
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

int main(void) {
    const uint64_t edges[] = {
        0, 1, 2, UINT64_C(0x000fffffffffffff), UINT64_C(0x0010000000000000),
        UINT64_C(0x3fefffffffffffff), UINT64_C(0x3ff0000000000000),
        UINT64_C(0x3ff0000000000001), UINT64_C(0x4008000000000000),
        UINT64_C(0x7fefffffffffffff), UINT64_C(0x7ff0000000000000),
        UINT64_C(0x7ff8000000000001), UINT64_C(0x7ff0000000000001)
    };
    for (unsigned i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i)
        for (unsigned j = 0; j < sizeof(edges)/sizeof(edges[0]); ++j)
            for (unsigned signs = 0; signs < 4; ++signs)
                check((Binary64){.bits = edges[i] | ((uint64_t)(signs & 1) << 63)},
                      (Binary64){.bits = edges[j] | ((uint64_t)(signs >> 1) << 63)});
    for (unsigned i = 0; i < 200000; ++i)
        check((Binary64){.bits = random_bits()}, (Binary64){.bits = random_bits()});
    puts("PASS: 676 edge pairs and 200000 deterministic binary64 pairs match libm, including signed zero");
    return 0;
}
