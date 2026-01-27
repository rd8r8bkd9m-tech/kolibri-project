/*
 * Kolibri Generation Engine
 * Step 5: Algorithmic Text Generation (Deterministic)
 * 
 * Implements:
 * - Genome Loading
 * - Concept Expansion (ID -> Sequence)
 * - Deterministic Continuation (Suffix Matching)
 */

#include "kolibri/decimal.h"
#include "kolibri/digit_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define MAX_PATTERNS 10000
#define PATTERN_MAX_LEN 32  // Increased for better output

typedef struct {
    uint32_t id;
    uint8_t digits[PATTERN_MAX_LEN];
    size_t length;
    size_t frequency;
} Pattern;

Pattern patterns[MAX_PATTERNS];
size_t pattern_count = 0;

void load_genome(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("Failed to load genome: %s. Run kolibri_learn first.\n", filename);
        exit(1);
    }
    
    char line[256];
    fgets(line, sizeof(line), f); // Header
    
    while (fgets(line, sizeof(line), f)) {
        if (pattern_count >= MAX_PATTERNS) break;
        
        unsigned int id;
        size_t freq;
        char seq_hex[128];
        
        // Format: ID:1000 SEQ:010203... FREQ:10
        if (sscanf(line, "ID:%X SEQ:%s FREQ:%zu", &id, seq_hex, &freq) == 3) {
            patterns[pattern_count].id = id;
            patterns[pattern_count].frequency = freq;
            
            size_t hex_len = strlen(seq_hex);
            patterns[pattern_count].length = hex_len / 2;
            
            for (size_t i=0; i < hex_len/2; i++) {
                unsigned int byte_val;
                sscanf(seq_hex + 2*i, "%02X", &byte_val);
                patterns[pattern_count].digits[i] = (uint8_t)byte_val;
            }
            pattern_count++;
        }
    }
    fclose(f);
    printf("Loaded %zu patterns from genome.\n", pattern_count);
}

// Helper to convert digits to text for display
void print_digits_as_text(const uint8_t *digits, size_t len) {
    // We reuse the logic from decimal.c: 3 digits = 1 byte
    // But our pattern miner worked on raw digits. 
    // If the input was normalized via decimal.c, it's already in 3-digit tuples.
    // However, the learner saw *any* N-gram.
    // So we might split a byte in half. 
    // For visualization, we just print the raw bytes if they align, or [?]
    
    // We assume the generator tries to align to byte boundaries (mod 3)
    
    if (len % 3 == 0) {
        char *buf = malloc(len/3 + 1);
        for(size_t i=0; i<len/3; i++) {
             int val = digits[i*3]*100 + digits[i*3+1]*10 + digits[i*3+2];
             buf[i] = (val >= 32 && val <= 126) ? (char)val : '.';
        }
        buf[len/3] = 0;
        printf("'%s'", buf);
        free(buf);
    } else {
        printf("[Fragm: ");
        for(size_t i=0; i<len; i++) printf("%d", digits[i]);
        printf("]");
    }
}

int match_suffix(const uint8_t *buffer, size_t buf_len, const uint8_t *pattern, size_t pat_len) {
    // Check if pattern starts with the suffix of buffer
    // Overlap size = 1 to min(len)
    
    size_t max_overlap = (buf_len < pat_len) ? buf_len : pat_len;
    // We want at least some overlap to "chain"
    if (max_overlap < 2) return 0;
    
    // Try to find largest overlap
    for (size_t ov = max_overlap; ov >= 2; ov--) {
        // Suffix of buffer
        const uint8_t *suffix = buffer + (buf_len - ov);
        // Prefix of pattern
        const uint8_t *prefix = pattern;
        
        if (memcmp(suffix, prefix, ov) == 0) {
            return (int)ov;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <start_phrase>\n", argv[0]);
        return 1;
    }
    
    load_genome("kolibri.genome");
    
    // Convert input to decimal
    KolibriDigitText seed;
    kolibri_digit_text_init(&seed);
    kolibri_digit_text_assign_utf8(&seed, argv[1]);
    
    printf("\nSeed: \"%s\"\n", argv[1]);
    printf("Decimal Structure: ");
    for(size_t i=0; i<seed.length; i++) printf("%d", seed.digits[i]);
    printf("\n\n=== Generating (Algorithmic Expansion) ===\n\n");
    
    // Generation Loop
    uint8_t *buffer = malloc(4096);
    memcpy(buffer, seed.digits, seed.length);
    size_t current_len = seed.length;
    
    printf("%s", argv[1]); // Echo start
    
    srand(time(NULL));
    
    for (int step=0; step<10; step++) {
        // Find best pattern to extend
        int best_pat = -1;
        int max_overlap = 0;
        
        // Simple beam search (width 1 for now)
        for (size_t i=0; i<pattern_count; i++) {
             int ov = match_suffix(buffer, current_len, patterns[i].digits, patterns[i].length);
             if (ov > max_overlap) {
                 max_overlap = ov;
                 best_pat = (int)i;
             }
        }
        
        if (best_pat != -1) {
            // Append non-overlapping part
            size_t append_len = patterns[best_pat].length - max_overlap;
            
            if (append_len > 0) {
                // Visualize concept
                // printf(" [ID:%04X] ", patterns[best_pat].id); 
                
                // Append
                memcpy(buffer + current_len, patterns[best_pat].digits + max_overlap, append_len);
                
                // Print only the new full bytes
                // We track where the last full byte boundary was
                size_t old_byte_boundary = current_len / 3;
                size_t new_byte_boundary = (current_len + append_len) / 3;
                
                for (size_t b = old_byte_boundary; b < new_byte_boundary; b++) {
                    int val = buffer[b*3]*100 + buffer[b*3+1]*10 + buffer[b*3+2];
                    if (val==0) break; 
                    printf("%c", (char)val);
                }
                
                current_len += append_len;
            } else {
                 break; // Fully contained
            }
        } else {
            break; // No continuation found
        }
    }
    
    printf("\n\n=== Generation Complete ===\n");
    
    free(buffer);
    kolibri_digit_text_free(&seed);
    
    return 0;
}
