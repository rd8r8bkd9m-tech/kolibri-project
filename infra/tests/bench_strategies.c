/*
 * Benchmark: Different optimization strategies for decimal encoding
 * Tests: division (original), LUT, batch processing, SIMD-like approaches
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define BENCH_ITERATIONS 50
#define TEST_SIZE (10 * 1024 * 1024)

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Strategy 1: Simple byte-by-byte division (current) */
static int encode_simple(unsigned char *output, size_t *out_len,
                         const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; i++) {
        unsigned char byte = input[i];
        output[out_pos++] = byte / 100;
        output[out_pos++] = (byte % 100) / 10;
        output[out_pos++] = byte % 10;
    }
    *out_len = out_pos;
    return 0;
}

/* Strategy 2: 8x unrolled simple */
static int encode_unroll8(unsigned char *output, size_t *out_len,
                          const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i + 8 <= in_len) {
        unsigned char b0 = input[i + 0], b1 = input[i + 1], b2 = input[i + 2], b3 = input[i + 3];
        unsigned char b4 = input[i + 4], b5 = input[i + 5], b6 = input[i + 6], b7 = input[i + 7];
        
        output[out_pos++] = b0 / 100; output[out_pos++] = (b0 % 100) / 10; output[out_pos++] = b0 % 10;
        output[out_pos++] = b1 / 100; output[out_pos++] = (b1 % 100) / 10; output[out_pos++] = b1 % 10;
        output[out_pos++] = b2 / 100; output[out_pos++] = (b2 % 100) / 10; output[out_pos++] = b2 % 10;
        output[out_pos++] = b3 / 100; output[out_pos++] = (b3 % 100) / 10; output[out_pos++] = b3 % 10;
        output[out_pos++] = b4 / 100; output[out_pos++] = (b4 % 100) / 10; output[out_pos++] = b4 % 10;
        output[out_pos++] = b5 / 100; output[out_pos++] = (b5 % 100) / 10; output[out_pos++] = b5 % 10;
        output[out_pos++] = b6 / 100; output[out_pos++] = (b6 % 100) / 10; output[out_pos++] = b6 % 10;
        output[out_pos++] = b7 / 100; output[out_pos++] = (b7 % 100) / 10; output[out_pos++] = b7 % 10;
        
        i += 8;
    }
    while (i < in_len) {
        unsigned char byte = input[i];
        output[out_pos++] = byte / 100;
        output[out_pos++] = (byte % 100) / 10;
        output[out_pos++] = byte % 10;
        i++;
    }
    *out_len = out_pos;
    return 0;
}

/* Strategy 3: Pre-computed LUT (768 bytes) */
static const uint8_t DIGITS_LUT[256][3] = {
    {0,0,0},{0,0,1},{0,0,2},{0,0,3},{0,0,4},{0,0,5},{0,0,6},{0,0,7},{0,0,8},{0,0,9},
    {0,1,0},{0,1,1},{0,1,2},{0,1,3},{0,1,4},{0,1,5},{0,1,6},{0,1,7},{0,1,8},{0,1,9},
    {0,2,0},{0,2,1},{0,2,2},{0,2,3},{0,2,4},{0,2,5},{0,2,6},{0,2,7},{0,2,8},{0,2,9},
    {0,3,0},{0,3,1},{0,3,2},{0,3,3},{0,3,4},{0,3,5},{0,3,6},{0,3,7},{0,3,8},{0,3,9},
    {0,4,0},{0,4,1},{0,4,2},{0,4,3},{0,4,4},{0,4,5},{0,4,6},{0,4,7},{0,4,8},{0,4,9},
    {0,5,0},{0,5,1},{0,5,2},{0,5,3},{0,5,4},{0,5,5},{0,5,6},{0,5,7},{0,5,8},{0,5,9},
    {0,6,0},{0,6,1},{0,6,2},{0,6,3},{0,6,4},{0,6,5},{0,6,6},{0,6,7},{0,6,8},{0,6,9},
    {0,7,0},{0,7,1},{0,7,2},{0,7,3},{0,7,4},{0,7,5},{0,7,6},{0,7,7},{0,7,8},{0,7,9},
    {0,8,0},{0,8,1},{0,8,2},{0,8,3},{0,8,4},{0,8,5},{0,8,6},{0,8,7},{0,8,8},{0,8,9},
    {0,9,0},{0,9,1},{0,9,2},{0,9,3},{0,9,4},{0,9,5},{0,9,6},{0,9,7},{0,9,8},{0,9,9},
    {1,0,0},{1,0,1},{1,0,2},{1,0,3},{1,0,4},{1,0,5},{1,0,6},{1,0,7},{1,0,8},{1,0,9},
    {1,1,0},{1,1,1},{1,1,2},{1,1,3},{1,1,4},{1,1,5},{1,1,6},{1,1,7},{1,1,8},{1,1,9},
    {1,2,0},{1,2,1},{1,2,2},{1,2,3},{1,2,4},{1,2,5},{1,2,6},{1,2,7},{1,2,8},{1,2,9},
    {1,3,0},{1,3,1},{1,3,2},{1,3,3},{1,3,4},{1,3,5},{1,3,6},{1,3,7},{1,3,8},{1,3,9},
    {1,4,0},{1,4,1},{1,4,2},{1,4,3},{1,4,4},{1,4,5},{1,4,6},{1,4,7},{1,4,8},{1,4,9},
    {1,5,0},{1,5,1},{1,5,2},{1,5,3},{1,5,4},{1,5,5},{1,5,6},{1,5,7},{1,5,8},{1,5,9},
    {1,6,0},{1,6,1},{1,6,2},{1,6,3},{1,6,4},{1,6,5},{1,6,6},{1,6,7},{1,6,8},{1,6,9},
    {1,7,0},{1,7,1},{1,7,2},{1,7,3},{1,7,4},{1,7,5},{1,7,6},{1,7,7},{1,7,8},{1,7,9},
    {1,8,0},{1,8,1},{1,8,2},{1,8,3},{1,8,4},{1,8,5},{1,8,6},{1,8,7},{1,8,8},{1,8,9},
    {1,9,0},{1,9,1},{1,9,2},{1,9,3},{1,9,4},{1,9,5},{1,9,6},{1,9,7},{1,9,8},{1,9,9},
    {2,0,0},{2,0,1},{2,0,2},{2,0,3},{2,0,4},{2,0,5},{2,0,6},{2,0,7},{2,0,8},{2,0,9},
    {2,1,0},{2,1,1},{2,1,2},{2,1,3},{2,1,4},{2,1,5},{2,1,6},{2,1,7},{2,1,8},{2,1,9},
    {2,2,0},{2,2,1},{2,2,2},{2,2,3},{2,2,4},{2,2,5},{2,2,6},{2,2,7},{2,2,8},{2,2,9},
    {2,3,0},{2,3,1},{2,3,2},{2,3,3},{2,3,4},{2,3,5},{2,3,6},{2,3,7},{2,3,8},{2,3,9},
    {2,4,0},{2,4,1},{2,4,2},{2,4,3},{2,4,4},{2,4,5},{2,4,6},{2,4,7},{2,4,8},{2,4,9},
    {2,5,0},{2,5,1},{2,5,2},{2,5,3},{2,5,4},{2,5,5},{2,5,6},{2,5,7},{2,5,8},{2,5,9},
};

static int encode_lut(unsigned char *output, size_t *out_len,
                      const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; i++) {
        const uint8_t *d = DIGITS_LUT[input[i]];
        output[out_pos++] = d[0];
        output[out_pos++] = d[1];
        output[out_pos++] = d[2];
    }
    *out_len = out_pos;
    return 0;
}

/* Benchmark helper */
typedef int (*encode_func)(unsigned char*, size_t*, const unsigned char*, size_t);

double benchmark_encode(encode_func func, const char *name,
                       unsigned char *input, unsigned char *output, size_t test_size) {
    size_t tmp_len;
    
    /* Warmup */
    for (int i = 0; i < 2; i++) {
        func(output, &tmp_len, input, test_size);
    }
    
    printf("  %s:\n", name);
    uint64_t total_ns = 0;
    
    for (int iter = 1; iter <= BENCH_ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        func(output, &tmp_len, input, test_size);
        uint64_t elapsed = get_time_ns() - start;
        total_ns += elapsed;
        
        if (iter <= 5 || iter % 10 == 0) {
            double mb_s = (test_size * 3 / 1e6) / (elapsed / 1e9);
            printf("    Iter %2d: %8.2f MB/s\n", iter, mb_s);
        }
    }
    
    double avg_ns = (double)total_ns / BENCH_ITERATIONS;
    double mb_s = (test_size * 3 / 1e6) / (avg_ns / 1e9);
    double chars_s = (test_size * 3) / (avg_ns / 1e9);
    
    printf("    Average: %.2f MB/s (%.2e chars/sec)\n\n", mb_s, chars_s);
    
    return chars_s;
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║    DECIMAL ENCODING STRATEGIES - COMPARATIVE ANALYSIS     ║\n");
    printf("║    Testing 4 different optimization approaches            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char *input = malloc(TEST_SIZE);
    unsigned char *output = malloc(TEST_SIZE * 3);
    
    if (!input || !output) {
        printf("❌ Failed to allocate\n");
        return 1;
    }
    
    for (size_t i = 0; i < TEST_SIZE; i++) {
        input[i] = (unsigned char)((i * 17 + 42) % 256);
    }
    
    printf("  Input: %d MB\n", TEST_SIZE / (1024 * 1024));
    printf("  Iterations: %d\n\n", BENCH_ITERATIONS);
    
    /* Verify all produce same output */
    size_t len1, len2, len3, len4;
    encode_simple(output, &len1, input, TEST_SIZE);
    unsigned char *verify1 = malloc(len1);
    memcpy(verify1, output, len1);
    
    encode_unroll8(output, &len2, input, TEST_SIZE);
    if (len2 != len1 || memcmp(verify1, output, len1) != 0) {
        printf("❌ Unroll8 output differs!\n");
        return 1;
    }
    
    encode_lut(output, &len3, input, TEST_SIZE);
    if (len3 != len1 || memcmp(verify1, output, len1) != 0) {
        printf("❌ LUT output differs!\n");
        return 1;
    }
    
    printf("  ✓ All strategies produce identical output\n\n");
    
    /* Benchmark */
    printf("📊 PERFORMANCE COMPARISON:\n\n");
    
    double chars_simple = benchmark_encode(encode_simple, "1. Simple (division)", 
                                          input, output, TEST_SIZE);
    double chars_unroll8 = benchmark_encode(encode_unroll8, "2. 8x Unroll (division)",
                                           input, output, TEST_SIZE);
    double chars_lut = benchmark_encode(encode_lut, "3. LUT (lookup table)",
                                        input, output, TEST_SIZE);
    
    printf("📈 SUMMARY:\n\n");
    printf("  Simple:    %.2e chars/sec (baseline)\n", chars_simple);
    printf("  Unroll8:   %.2e chars/sec (%.2fx)\n", chars_unroll8, chars_unroll8 / chars_simple);
    printf("  LUT:       %.2e chars/sec (%.2fx)\n", chars_lut, chars_lut / chars_simple);
    
    double best = (chars_simple > chars_unroll8) ? 
                  ((chars_simple > chars_lut) ? chars_simple : chars_lut) :
                  ((chars_unroll8 > chars_lut) ? chars_unroll8 : chars_lut);
    
    printf("\n  ✓ Best strategy: %.2e chars/sec\n", best);
    
    free(input);
    free(output);
    free(verify1);
    return 0;
}
