#include "kolibri_hash_128.h"

/* 
 * Toy/Demo Bijective 128-bit Hash based on a 4-round Feistel Network.
 * This structure guarantees invertibility regardless of the round function.
 */
KolibriHash128 kolibri_hash_128(KolibriKey128 key) {
    KolibriHash128 h;
    uint64_t l = key.low;
    uint64_t r = key.high;
    uint64_t temp;
    
    // Round keys (arbitrary constants for demo)
    const uint64_t k1 = 0xff51afd7ed558ccdULL;
    const uint64_t k2 = 0xc4ceb9fe1a85ec53ULL;
    const uint64_t k3 = 0x85ebca6b64e5a0cdULL;
    const uint64_t k4 = 0x9cb4b2f8129337dbULL;

    // Round 1: L1 = R0, R1 = L0 ^ F(R0, K1)
    temp = r;
    r = l ^ (r * k1);
    l = temp;

    // Round 2
    temp = r;
    r = l ^ (r * k2);
    l = temp;

    // Round 3
    temp = r;
    r = l ^ (r * k3);
    l = temp;

    // Round 4
    temp = r;
    r = l ^ (r * k4);
    l = temp;
    
    h.low = l;
    h.high = r;
    return h;
}

/* 
 * Inverse function for the Feistel-based hash.
 * Simply reverses the rounds and the operations.
 */
int kolibri_unhash_128_demo(KolibriHash128 hash, KolibriKey128 *out_key) {
    if (!out_key) return -1;

    uint64_t l = hash.low;
    uint64_t r = hash.high;
    uint64_t temp;
    
    const uint64_t k1 = 0xff51afd7ed558ccdULL;
    const uint64_t k2 = 0xc4ceb9fe1a85ec53ULL;
    const uint64_t k3 = 0x85ebca6b64e5a0cdULL;
    const uint64_t k4 = 0x9cb4b2f8129337dbULL;

    // Inverse Round 4
    temp = l; // Because l was set to old r
    l = r ^ (l * k4); // Recover old l
    r = temp;

    // Inverse Round 3
    temp = l;
    l = r ^ (l * k3);
    r = temp;

    // Inverse Round 2
    temp = l;
    l = r ^ (l * k2);
    r = temp;

    // Inverse Round 1
    temp = l;
    l = r ^ (l * k1);
    r = temp;
    
    out_key->low = l;
    out_key->high = r;
    
    return 0; // Success (Invertible)
}
