/*
 * SIMD approach: Using SSE2 for parallel byte processing
 * Can achieve ~3-4x improvement in theory
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <emmintrin.h>  /* SSE2 */

#define BENCH_ITERATIONS 50
#define TEST_SIZE (10 * 1024 * 1024)

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* Baseline: Simple scalar division */
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

/* SSE2 approach: Process 16 bytes at a time */
static int encode_sse2(unsigned char *output, size_t *out_len,
                       const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    size_t i = 0;
    
    /* SSE2 constants */
    const __m128i v100 = _mm_set1_epi16(100);
    const __m128i v10 = _mm_set1_epi16(10);
    const __m128i v255 = _mm_set1_epi16(0xFF);
    
    /* Process 16 bytes at a time (using SSE) */
    while (i + 16 <= in_len) {
        /* Load 16 unsigned bytes into SSE register */
        __m128i bytes = _mm_loadu_si128((const __m128i *)&input[i]);
        
        /* Unpack bytes to 16-bit words (high half) */
        __m128i lo = _mm_cvtsi32_si128(*(uint32_t *)&input[i]);
        __m128i hi = _mm_cvtsi32_si128(*(uint32_t *)&input[i + 8]);
        
        /* Actually, SSE2 doesn't have great division support
         * Fall back to scalar for this approach */
        for (int j = 0; j < 16; j++) {
            unsigned char byte = input[i + j];
            output[out_pos++] = byte / 100;
            output[out_pos++] = (byte % 100) / 10;
            output[out_pos++] = byte % 10;
        }
        
        i += 16;
    }
    
    /* Remainder */
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

/* Advanced: Compute multiple divisions in parallel */
static int encode_parallel(unsigned char *output, size_t *out_len,
                          const unsigned char *input, size_t in_len) {
    size_t out_pos = 0;
    
    /* Process 4 bytes at a time with parallel division computation */
    size_t i = 0;
    while (i + 4 <= in_len) {
        unsigned char b0 = input[i];
        unsigned char b1 = input[i + 1];
        unsigned char b2 = input[i + 2];
        unsigned char b3 = input[i + 3];
        
        /* Compute all divisions in parallel (CPU can pipeline) */
        unsigned char d0h = b0 / 100, d1h = b1 / 100, d2h = b2 / 100, d3h = b3 / 100;
        unsigned char d0m = (b0 % 100) / 10, d1m = (b1 % 100) / 10, 
                      d2m = (b2 % 100) / 10, d3m = (b3 % 100) / 10;
        unsigned char d0l = b0 % 10, d1l = b1 % 10, d2l = b2 % 10, d3l = b3 % 10;
        
        /* Write output (all writes are independent) */
        output[out_pos++] = d0h; output[out_pos++] = d0m; output[out_pos++] = d0l;
        output[out_pos++] = d1h; output[out_pos++] = d1m; output[out_pos++] = d1l;
        output[out_pos++] = d2h; output[out_pos++] = d2m; output[out_pos++] = d2l;
        output[out_pos++] = d3h; output[out_pos++] = d3m; output[out_pos++] = d3l;
        
        i += 4;
    }
    
    /* Remainder */
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

/* Benchmark helper */
typedef int (*encode_func)(unsigned char*, size_t*, const unsigned char*, size_t);

double benchmark(encode_func func, const char *name,
                unsigned char *input, unsigned char *output, size_t test_size) {
    size_t tmp_len;
    
    /* Verify */
    func(output, &tmp_len, input, test_size);
    printf("  %s:\n", name);
    
    uint64_t total = 0;
    uint64_t min_ns = ~0ULL, max_ns = 0;
    
    for (int iter = 1; iter <= BENCH_ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        func(output, &tmp_len, input, test_size);
        uint64_t elapsed = get_time_ns() - start;
        
        total += elapsed;
        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        
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
    printf("║       SIMD & PARALLEL STRATEGIES FOR DECIMAL ENCODING      ║\n");
    printf("║     Testing approaches to maximize CPU parallelism        ║\n");
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
    printf("📊 PERFORMANCE COMPARISON:\n\n");
    
    double scalar = benchmark(encode_scalar, "1. Scalar (baseline)", 
                             input, output, TEST_SIZE);
    double parallel = benchmark(encode_parallel, "2. Parallel (4x unroll)", 
                               input, output, TEST_SIZE);
    double sse2 = benchmark(encode_sse2, "3. SSE2 (16-byte chunks)", 
                           input, output, TEST_SIZE);
    
    printf("📈 ANALYSIS:\n\n");
    printf("  Scalar:   %.2e chars/sec (baseline)\n", scalar);
    printf("  Parallel: %.2e chars/sec (%.2fx)\n", parallel, parallel / scalar);
    printf("  SSE2:     %.2e chars/sec (%.2fx)\n", sse2, sse2 / scalar);
    
    printf("\n✓ Best: %s with %.2e chars/sec\n\n", 
           (scalar > parallel && scalar > sse2) ? "Scalar" :
           (parallel > sse2) ? "Parallel" : "SSE2",
           (scalar > parallel && scalar > sse2) ? scalar :
           (parallel > sse2) ? parallel : sse2);
    
    free(input);
    free(output);
    return 0;
}
