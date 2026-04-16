/*
 * ARM NEON approach: For Apple Silicon M1/M2
 * Tests to find if NEON can provide speedup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <arm_neon.h>

#define BENCH_ITERATIONS 50
#define TEST_SIZE (10 * 1024 * 1024)

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Baseline: Scalar division */
static int encode_scalar(unsigned char *output, size_t *out_len,
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

/* Parallel: 4-byte chunks with loop unrolling */
static int encode_parallel(unsigned char *output, size_t *out_len,
                          const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    size_t i = 0;
    
    while (i + 4 <= in_len) {
        unsigned char b0 = input[i];
        unsigned char b1 = input[i + 1];
        unsigned char b2 = input[i + 2];
        unsigned char b3 = input[i + 3];
        
        output[out_pos++] = b0 / 100; output[out_pos++] = (b0 % 100) / 10; output[out_pos++] = b0 % 10;
        output[out_pos++] = b1 / 100; output[out_pos++] = (b1 % 100) / 10; output[out_pos++] = b1 % 10;
        output[out_pos++] = b2 / 100; output[out_pos++] = (b2 % 100) / 10; output[out_pos++] = b2 % 10;
        output[out_pos++] = b3 / 100; output[out_pos++] = (b3 % 100) / 10; output[out_pos++] = b3 % 10;
        
        i += 4;
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

/* NEON: Process 16 bytes at a time using ARM NEON */
static int encode_neon(unsigned char *output, size_t *out_len,
                      const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    size_t i = 0;
    
    /* NEON constants */
    const uint8x16_t v100 = vdupq_n_u8(100);
    const uint8x16_t v10 = vdupq_n_u8(10);
    
    while (i + 16 <= in_len) {
        /* Load 16 bytes */
        uint8x16_t bytes = vld1q_u8(&input[i]);
        
        /* Divide by 100 (high digit) */
        uint8x16_t d0 = vdivq_u8(bytes, v100);
        
        /* Modulo 100 */
        uint8x16_t mod100 = vsubq_u8(bytes, vmulq_u8(d0, v100));
        
        /* Divide by 10 (middle digit) */
        uint8x16_t d1 = vdivq_u8(mod100, v10);
        
        /* Modulo 10 (low digit) */
        uint8x16_t d2 = vsubq_u8(mod100, vmulq_u8(d1, v10));
        
        /* Write output - need to interleave 3 vectors */
        /* NEON doesn't have native division, so fall back to scalar */
        for (int j = 0; j < 16; j++) {
            unsigned char byte = input[i + j];
            output[out_pos++] = byte / 100;
            output[out_pos++] = (byte % 100) / 10;
            output[out_pos++] = byte % 10;
        }
        
        i += 16;
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

typedef int (*encode_func)(unsigned char*, size_t*, const unsigned char*, size_t);

double benchmark(encode_func func, const char *name,
                unsigned char *input, unsigned char *output, size_t test_size) {
    size_t tmp_len;
    func(output, &tmp_len, input, test_size);
    printf("  %s:\n", name);
    
    uint64_t total = 0;
    for (int iter = 1; iter <= BENCH_ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        func(output, &tmp_len, input, test_size);
        uint64_t elapsed = get_time_ns() - start;
        total += elapsed;
        
        if (iter <= 5 || iter % 10 == 0) {
            double mb = (test_size * 3 / 1e6) / (elapsed / 1e9);
            printf("    Iter %2d: %8.2f MB/s\n", iter, mb);
        }
    }
    
    double avg_ns = (double)total / BENCH_ITERATIONS;
    double chars_s = (test_size * 3) / (avg_ns / 1e9);
    printf("    Average: %.2e chars/sec\n\n", chars_s);
    
    return chars_s;
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║       ARM NEON OPTIMIZATIONS FOR APPLE SILICON M1        ║\n");
    printf("║     Testing vectorized approaches on ARM64 architecture   ║\n");
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
    printf("  Iterations: %d\n", BENCH_ITERATIONS);
    printf("  CPU: Apple M1 (ARM64)\n\n");
    printf("📊 PERFORMANCE COMPARISON:\n\n");
    
    double scalar = benchmark(encode_scalar, "1. Scalar (baseline)", input, output, TEST_SIZE);
    double parallel = benchmark(encode_parallel, "2. Parallel unroll (4x)", input, output, TEST_SIZE);
    double neon = benchmark(encode_neon, "3. NEON (16-byte chunks)", input, output, TEST_SIZE);
    
    printf("📈 ANALYSIS:\n\n");
    printf("  Scalar:   %.2e chars/sec (baseline 100%%)\n", scalar);
    printf("  Parallel: %.2e chars/sec (%.1f%%)\n", parallel, (parallel / scalar) * 100);
    printf("  NEON:     %.2e chars/sec (%.1f%%)\n", neon, (neon / scalar) * 100);
    
    double best = (scalar > parallel && scalar > neon) ? scalar :
                  (parallel > neon) ? parallel : neon;
    
    printf("\n✓ Best approach: %.2e chars/sec\n\n", best);
    
    free(input);
    free(output);
    return 0;
}
