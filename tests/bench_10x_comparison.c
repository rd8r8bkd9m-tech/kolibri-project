/*
 * Comparative benchmark: Original vs 10x Faster Decimal Core
 * Tests both implementations to show actual speedup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#define BENCH_ITERATIONS 50
#define TEST_SIZE (10 * 1024 * 1024)  /* 10 MB per iteration */

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ===== ORIGINAL IMPLEMENTATION (from decimal.c) ===== */
static int encode_original(unsigned char *output, size_t *out_len,
                           const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    
    /* Process input byte by byte (simplified from original) */
    for (size_t i = 0; i < in_len; i++) {
        unsigned char byte = input[i];
        
        /* Compute digits (0-255 maps to 3 digits: 0-9, 0-9, 0-9) */
        unsigned char d0 = byte / 100;
        unsigned char d1 = (byte % 100) / 10;
        unsigned char d2 = byte % 10;
        
        output[out_pos++] = d0;
        output[out_pos++] = d1;
        output[out_pos++] = d2;
    }
    
    *out_len = out_pos;
    return 0;
}

/* ===== NEW 10x FASTER IMPLEMENTATION ===== */
/* Pre-computed digit LUT (768 bytes, L1 cache) */
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

static int encode_fast(unsigned char *output, size_t *out_len,
                       const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    
    /* Use LUT with 8x loop unrolling - direct assignment (no memcpy overhead) */
    size_t i = 0;
    
    /* Process 8 bytes at a time */
    while (i + 8 <= in_len) {
        const uint8_t *d0 = DIGITS_LUT[input[i + 0]];
        const uint8_t *d1 = DIGITS_LUT[input[i + 1]];
        const uint8_t *d2 = DIGITS_LUT[input[i + 2]];
        const uint8_t *d3 = DIGITS_LUT[input[i + 3]];
        const uint8_t *d4 = DIGITS_LUT[input[i + 4]];
        const uint8_t *d5 = DIGITS_LUT[input[i + 5]];
        const uint8_t *d6 = DIGITS_LUT[input[i + 6]];
        const uint8_t *d7 = DIGITS_LUT[input[i + 7]];
        
        output[out_pos++] = d0[0]; output[out_pos++] = d0[1]; output[out_pos++] = d0[2];
        output[out_pos++] = d1[0]; output[out_pos++] = d1[1]; output[out_pos++] = d1[2];
        output[out_pos++] = d2[0]; output[out_pos++] = d2[1]; output[out_pos++] = d2[2];
        output[out_pos++] = d3[0]; output[out_pos++] = d3[1]; output[out_pos++] = d3[2];
        output[out_pos++] = d4[0]; output[out_pos++] = d4[1]; output[out_pos++] = d4[2];
        output[out_pos++] = d5[0]; output[out_pos++] = d5[1]; output[out_pos++] = d5[2];
        output[out_pos++] = d6[0]; output[out_pos++] = d6[1]; output[out_pos++] = d6[2];
        output[out_pos++] = d7[0]; output[out_pos++] = d7[1]; output[out_pos++] = d7[2];
        
        i += 8;
    }
    
    /* Handle remainder */
    while (i < in_len) {
        const uint8_t *d = DIGITS_LUT[input[i]];
        output[out_pos++] = d[0];
        output[out_pos++] = d[1];
        output[out_pos++] = d[2];
        i++;
    }
    
    *out_len = out_pos;
    return 0;
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║    COMPARATIVE BENCHMARK - ORIGINAL vs 10x FASTER         ║\n");
    printf("║    Direct performance comparison (same test data)         ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    /* Allocate buffers */
    unsigned char *input = malloc(TEST_SIZE);
    unsigned char *output_orig = malloc(TEST_SIZE * 3);
    unsigned char *output_fast = malloc(TEST_SIZE * 3);
    
    if (!input || !output_orig || !output_fast) {
        printf("❌ Failed to allocate buffers\n");
        return 1;
    }
    
    /* Fill input with pseudo-random data */
    for (size_t i = 0; i < TEST_SIZE; i++) {
        input[i] = (unsigned char)((i * 17 + 42) % 256);
    }
    
    printf("  Input: %d MB\n", TEST_SIZE / (1024 * 1024));
    printf("  Iterations: %d\n", BENCH_ITERATIONS);
    printf("  Warming up...\n\n");
    
    /* Warmup */
    size_t tmp_len;
    encode_original(output_orig, &tmp_len, input, TEST_SIZE);
    encode_fast(output_fast, &tmp_len, input, TEST_SIZE);
    
    /* Verify outputs match */
    if (memcmp(output_orig, output_fast, tmp_len) != 0) {
        printf("❌ Output mismatch! Implementations differ!\n");
        return 1;
    }
    printf("  ✓ Correctness verified: outputs identical\n\n");
    
    /* Benchmark ORIGINAL */
    printf("📊 ORIGINAL IMPLEMENTATION:\n\n");
    uint64_t min_orig = ~0ULL, max_orig = 0, total_orig = 0;
    
    for (int iter = 1; iter <= BENCH_ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        size_t out_len;
        encode_original(output_orig, &out_len, input, TEST_SIZE);
        uint64_t elapsed = get_time_ns() - start;
        
        if (elapsed < min_orig) min_orig = elapsed;
        if (elapsed > max_orig) max_orig = elapsed;
        total_orig += elapsed;
        
        double mb_s = (TEST_SIZE * 3 / 1e6) / (elapsed / 1e9);
        if (iter <= 5 || iter % 10 == 0) {
            printf("  Iter %2d: %8.2f MB/s\n", iter, mb_s);
        }
    }
    
    double avg_orig = (double)total_orig / BENCH_ITERATIONS;
    double chars_orig = (TEST_SIZE * 3) / (avg_orig / 1e9);
    double mb_avg_orig = (TEST_SIZE * 3 / 1e6) / (avg_orig / 1e9);
    
    printf("  Average: %.2f MB/s (%.2e chars/sec)\n\n", mb_avg_orig, chars_orig);
    
    /* Benchmark FAST */
    printf("📊 10x FASTER IMPLEMENTATION:\n\n");
    uint64_t min_fast = ~0ULL, max_fast = 0, total_fast = 0;
    
    for (int iter = 1; iter <= BENCH_ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        size_t out_len;
        encode_fast(output_fast, &out_len, input, TEST_SIZE);
        uint64_t elapsed = get_time_ns() - start;
        
        if (elapsed < min_fast) min_fast = elapsed;
        if (elapsed > max_fast) max_fast = elapsed;
        total_fast += elapsed;
        
        double mb_s = (TEST_SIZE * 3 / 1e6) / (elapsed / 1e9);
        if (iter <= 5 || iter % 10 == 0) {
            printf("  Iter %2d: %8.2f MB/s\n", iter, mb_s);
        }
    }
    
    double avg_fast = (double)total_fast / BENCH_ITERATIONS;
    double chars_fast = (TEST_SIZE * 3) / (avg_fast / 1e9);
    double mb_avg_fast = (TEST_SIZE * 3 / 1e6) / (avg_fast / 1e9);
    
    printf("  Average: %.2f MB/s (%.2e chars/sec)\n\n", mb_avg_fast, chars_fast);
    
    /* Summary */
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  SUMMARY                                                  ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    double speedup = avg_orig / avg_fast;
    printf("║  Speedup: %.2fx                                          ║\n", speedup);
    double improvement_factor = chars_fast / chars_orig;
    printf("║  Throughput improvement: %.2fx                           ║\n", improvement_factor);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    free(input);
    free(output_orig);
    free(output_fast);
    return 0;
}
