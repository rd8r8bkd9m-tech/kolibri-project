/*
 * Kolibri Learning Engine
 * Step 3: Pattern Mining & Genome Construction
 * 
 * Implements:
 * - N-gram Frequency Analysis (N=3..5)
 * - Pattern Extraction > Threshold
 * - Genome Serialization
 */

#include "kolibri/decimal.h"
#include "kolibri/digit_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_PATTERNS 10000
#define MIN_FREQ 2
#define PATTERN_MAX_LEN 16

typedef struct {
    uint8_t digits[PATTERN_MAX_LEN];
    size_t length;
    size_t frequency;
    uint32_t id;
} Pattern;

Pattern patterns[MAX_PATTERNS];
size_t pattern_count = 0;

// Simple Hash for sequences
uint64_t hash_seq(const uint8_t *data, size_t len) {
    uint64_t h = 5381;
    for(size_t i=0; i<len; i++) {
        h = ((h << 5) + h) + data[i];
    }
    return h;
}

int find_pattern(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < pattern_count; i++) {
        if (patterns[i].length == len && memcmp(patterns[i].digits, data, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void learn_from_digits(const uint8_t *digits, size_t len) {
    // Brute-force N-gram mining (N=3 to 5)
    // O(L*N*M) - Slow but correct for prototype
    
    for (size_t n = 3; n <= 5 && n <= len; n++) {
        for (size_t i = 0; i <= len - n; i++) {
            const uint8_t *window = digits + i;
            
            int idx = find_pattern(window, n);
            if (idx >= 0) {
                patterns[idx].frequency++;
            } else {
                if (pattern_count < MAX_PATTERNS) {
                    memcpy(patterns[pattern_count].digits, window, n);
                    patterns[pattern_count].length = n;
                    patterns[pattern_count].frequency = 1;
                    patterns[pattern_count].id = (uint32_t)(0x1000 + pattern_count); // Start IDs at 0x1000
                    pattern_count++;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <input_raw_dat>\n", argv[0]);
        return 1;
    }

    FILE *in = fopen(argv[1], "rb");
    if (!in) {
        fprintf(stderr, "Error opening %s\n", argv[1]);
        return 1;
    }

    char header[15];
    fread(header, 1, 14, in);
    header[14] = 0;
    if (strcmp(header, "KOLIBRI_RAW_V1") != 0) {
        fprintf(stderr, "Invalid header: %s\n", header);
        fclose(in);
        return 1;
    }

    printf("Learning from %s...\n", argv[1]);

    while (!feof(in)) {
        uint32_t len;
        if (fread(&len, sizeof(uint32_t), 1, in) != 1) break;
        
        uint8_t *buffer = malloc(len);
        if (fread(buffer, 1, len, in) != len) {
            free(buffer);
            break;
        }

        learn_from_digits(buffer, len);
        free(buffer);
    }
    fclose(in);

    // Filter and Save Genome
    printf("Total candidates: %zu\n", pattern_count);
    
    FILE *genome_out = fopen("kolibri.genome", "w");
    fprintf(genome_out, "KOLIBRI_GENOME_V1\n");
    
    size_t saved = 0;
    for (size_t i = 0; i < pattern_count; i++) {
        // "Fitness Function": Freq * Length
        size_t score = patterns[i].frequency * patterns[i].length;
        
        if (score > 5) { // Threshold
            fprintf(genome_out, "ID:%04X SEQ:", patterns[i].id);
            for(size_t k=0; k<patterns[i].length; k++) {
                fprintf(genome_out, "%02X", patterns[i].digits[k]);
            }
            fprintf(genome_out, " FREQ:%zu\n", patterns[i].frequency);
            saved++;
        }
    }
    fclose(genome_out);
    
    printf("Learned Patterns: %zu (Saved to kolibri.genome)\n", saved);

    return 0;
}
