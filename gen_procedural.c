#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "kolibri/random.h"

int main() {
    FILE *f = fopen("test_genome_gen.dat", "wb");
    if (!f) return 1;

    // Use seed 12345
    KolibriRng rng;
    k_rng_seed(&rng, 12345);

    // Generate 16MB of data
    // This looks like random noise to Gzip/XZ
    for (size_t i = 0; i < 16 * 1024 * 1024; i++) {
        uint8_t byte = (uint8_t)(k_rng_next(&rng) % 256);
        fputc(byte, f);
    }
    
    fclose(f);
    printf("Generated 16MB procedural data (Seed 12345)\n");
    return 0;
}
