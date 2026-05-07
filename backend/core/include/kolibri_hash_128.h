#ifndef KOLIBRI_HASH_128_H
#define KOLIBRI_HASH_128_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t low;
    uint64_t high;
} KolibriKey128;

typedef struct {
    uint64_t low;
    uint64_t high;
} KolibriHash128;

/* 128-bit Hash Function (Simplified SipHash-like) */
KolibriHash128 kolibri_hash_128(KolibriKey128 key);

/* Demo Inversion Function (Toy/Reversible Hash Only) */
int kolibri_unhash_128_demo(KolibriHash128 hash, KolibriKey128 *out_key);

/* Helper to compare 128-bit values */
static inline int kolibri_key_compare(KolibriKey128 a, KolibriKey128 b) {
    if (a.high != b.high) return (a.high > b.high) ? 1 : -1;
    if (a.low != b.low) return (a.low > b.low) ? 1 : -1;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif // KOLIBRI_HASH_128_H
