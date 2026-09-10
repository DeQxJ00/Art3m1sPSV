#pragma once
#include <stdint.h>

/* Per-translation-unit, nonblocking diagnostic budget: 16 slow operations per
   five-second window. Startup cannot exhaust the rest of the session's quota.
   One 32-bit atomic keeps this usable on Vita without 64-bit atomic helpers. */
static inline int host_load_sample_allowed(uint32_t *state, uint64_t end_us) {
    uint32_t window = (uint32_t)(end_us / 5000000) & 0x00ffffffu;
    uint32_t old = __atomic_load_n(state, __ATOMIC_RELAXED);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t previous = old >> 8;
        uint32_t count = old & 255u;
        if (previous != window) {
            /* A delayed caller must not reopen a previous window. The modular
               comparison also handles the approximately 971-day wrap. */
            if (((window - previous) & 0x00ffffffu) >= 0x00800000u) return 0;
            count = 0;
        }
        if (count >= 16) return 0;
        uint32_t next = (window << 8) | (count + 1);
        if (__atomic_compare_exchange_n(state, &old, next, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return 1;
    }
    return 0; /* Suppress a sample rather than wait under diagnostic contention. */
}
