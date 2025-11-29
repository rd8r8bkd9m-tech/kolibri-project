/*
 * Kolibri Benchmark Suite v2.0
 * SIMD-Optimized CPU/GPU encoding benchmark with statistics
 * 
 * Features:
 * - 8x unrolled LUT encoding with prefetch (SIMD-style parallelism)
 * - 4x unrolled parallel decoding
 * - Statistics: min, max, avg, stddev, percentiles (p50, p95, p99)
 * - JSON and Markdown output formats
 * 
 * Copyright (c) 2025 Kolibri Project
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#include "kolibri/decimal.h"

/* Benchmark configuration */
#define WARMUP_ITERATIONS 3
#define MIN_ITERATIONS 5
#define MAX_ITERATIONS 20
#define TARGET_DURATION_MS 1000.0

/* Test sizes */
typedef struct {
    const char *name;
    size_t size;
} TestSize;

static const TestSize TEST_SIZES[] = {
    {"1KB", 1024},
    {"1MB", 1024 * 1024},
    {"10MB", 10 * 1024 * 1024},
    {"100MB", 100 * 1024 * 1024},
};
#define NUM_TEST_SIZES (sizeof(TEST_SIZES) / sizeof(TEST_SIZES[0]))

/* Statistics structure */
typedef struct {
    double min;
    double max;
    double avg;
    double stddev;
    double p50;
    double p95;
    double p99;
    double throughput_gbps;
    double chars_per_sec;
    int iterations;
} BenchStats;

/* Result structure */
typedef struct {
    const char *test_name;
    size_t data_size;
    BenchStats encode_stats;
    BenchStats decode_stats;
    BenchStats roundtrip_stats;
    int correctness_verified;
} BenchResult;

static BenchResult results[NUM_TEST_SIZES];
static int num_results = 0;

/* Pre-computed lookup table for ultra-fast encoding */
static const uint8_t DIGIT_LUT[256][3] = {
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
    {2,5,0},{2,5,1},{2,5,2},{2,5,3},{2,5,4},{2,5,5},
};

/* High-resolution timer */
static uint64_t get_time_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000LL) / freq.QuadPart);
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
#endif
}

/* Comparison function for qsort */
static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Calculate statistics from timing data */
static void calculate_stats(double *times, int count, size_t data_size, BenchStats *stats) {
    if (count == 0) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    
    /* Sort for percentiles */
    qsort(times, (size_t)count, sizeof(double), compare_double);
    
    /* Basic stats */
    stats->min = times[0];
    stats->max = times[count - 1];
    stats->iterations = count;
    
    /* Average */
    double sum = 0;
    for (int i = 0; i < count; i++) {
        sum += times[i];
    }
    stats->avg = sum / count;
    
    /* Standard deviation */
    double sq_sum = 0;
    for (int i = 0; i < count; i++) {
        double diff = times[i] - stats->avg;
        sq_sum += diff * diff;
    }
    stats->stddev = sqrt(sq_sum / count);
    
    /* Percentiles */
    stats->p50 = times[count / 2];
    stats->p95 = times[(int)(count * 0.95)];
    stats->p99 = times[(int)(count * 0.99)];
    
    /* Throughput (using average time in ms) */
    double avg_sec = stats->avg / 1000.0;
    if (avg_sec > 0) {
        stats->throughput_gbps = (data_size / (1024.0 * 1024.0 * 1024.0)) / avg_sec;
        stats->chars_per_sec = data_size / avg_sec;
    }
}

/* Ultra-fast LUT encode (baseline for comparison) */
__attribute__((unused))
static void encode_lut_fast(uint8_t *out, const unsigned char *in, size_t len) {
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i++) {
        const uint8_t *d = DIGIT_LUT[in[i]];
        out[out_pos++] = d[0];
        out[out_pos++] = d[1];
        out[out_pos++] = d[2];
    }
}

/* SIMD-style parallel encode: 8x unrolled with explicit parallelism */
static void encode_simd_parallel(uint8_t * restrict out, const unsigned char * restrict in, size_t len) {
    size_t i = 0;
    size_t out_pos = 0;
    
    /* Process 8 bytes at a time for maximum throughput */
    while (i + 8 <= len) {
        /* Prefetch next cache line */
        #ifdef __GNUC__
        __builtin_prefetch(&in[i + 64], 0, 3);
        #endif
        
        /* Load 8 bytes and their LUT entries in parallel */
        const uint8_t *d0 = DIGIT_LUT[in[i]];
        const uint8_t *d1 = DIGIT_LUT[in[i+1]];
        const uint8_t *d2 = DIGIT_LUT[in[i+2]];
        const uint8_t *d3 = DIGIT_LUT[in[i+3]];
        const uint8_t *d4 = DIGIT_LUT[in[i+4]];
        const uint8_t *d5 = DIGIT_LUT[in[i+5]];
        const uint8_t *d6 = DIGIT_LUT[in[i+6]];
        const uint8_t *d7 = DIGIT_LUT[in[i+7]];
        
        /* Write all 24 output bytes (8 * 3 digits) */
        out[out_pos++] = d0[0]; out[out_pos++] = d0[1]; out[out_pos++] = d0[2];
        out[out_pos++] = d1[0]; out[out_pos++] = d1[1]; out[out_pos++] = d1[2];
        out[out_pos++] = d2[0]; out[out_pos++] = d2[1]; out[out_pos++] = d2[2];
        out[out_pos++] = d3[0]; out[out_pos++] = d3[1]; out[out_pos++] = d3[2];
        out[out_pos++] = d4[0]; out[out_pos++] = d4[1]; out[out_pos++] = d4[2];
        out[out_pos++] = d5[0]; out[out_pos++] = d5[1]; out[out_pos++] = d5[2];
        out[out_pos++] = d6[0]; out[out_pos++] = d6[1]; out[out_pos++] = d6[2];
        out[out_pos++] = d7[0]; out[out_pos++] = d7[1]; out[out_pos++] = d7[2];
        
        i += 8;
    }
    
    /* Handle remainder */
    while (i < len) {
        const uint8_t *d = DIGIT_LUT[in[i++]];
        out[out_pos++] = d[0];
        out[out_pos++] = d[1];
        out[out_pos++] = d[2];
    }
}

/* Fast decode (baseline for comparison) */
__attribute__((unused))
static void decode_fast(unsigned char *out, const uint8_t *digits, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++) {
        size_t offset = i * 3;
        unsigned int value = digits[offset] * 100U + digits[offset + 1] * 10U + digits[offset + 2];
        out[i] = (unsigned char)value;
    }
}

/* SIMD-style parallel decode: 4x unrolled */
static void decode_simd_parallel(unsigned char * restrict out, const uint8_t * restrict digits, size_t byte_count) {
    size_t i = 0;
    
    /* Process 4 bytes at a time */
    while (i + 4 <= byte_count) {
        size_t o0 = i * 3;
        size_t o1 = o0 + 3;
        size_t o2 = o1 + 3;
        size_t o3 = o2 + 3;
        
        /* Compute all values in parallel */
        out[i]   = (unsigned char)(digits[o0] * 100U + digits[o0+1] * 10U + digits[o0+2]);
        out[i+1] = (unsigned char)(digits[o1] * 100U + digits[o1+1] * 10U + digits[o1+2]);
        out[i+2] = (unsigned char)(digits[o2] * 100U + digits[o2+1] * 10U + digits[o2+2]);
        out[i+3] = (unsigned char)(digits[o3] * 100U + digits[o3+1] * 10U + digits[o3+2]);
        
        i += 4;
    }
    
    /* Handle remainder */
    while (i < byte_count) {
        size_t offset = i * 3;
        out[i] = (unsigned char)(digits[offset] * 100U + digits[offset+1] * 10U + digits[offset+2]);
        i++;
    }
}

/* Run encoding benchmark */
static void run_encode_benchmark(const unsigned char *input, size_t size,
                                 uint8_t *output, BenchStats *stats) {
    double times[MAX_ITERATIONS];
    int iter = 0;
    
    /* Warmup phase */
    printf("    Warmup...");
    fflush(stdout);
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        encode_simd_parallel(output, input, size);
    }
    printf(" done\n");
    
    /* Benchmark phase */
    printf("    Running iterations: ");
    fflush(stdout);
    
    double total_time = 0;
    while (iter < MIN_ITERATIONS || (total_time < TARGET_DURATION_MS && iter < MAX_ITERATIONS)) {
        uint64_t start = get_time_ns();
        encode_simd_parallel(output, input, size);
        uint64_t end = get_time_ns();
        
        double elapsed_ms = (end - start) / 1e6;
        times[iter] = elapsed_ms;
        total_time += elapsed_ms;
        iter++;
        
        printf(".");
        fflush(stdout);
    }
    printf(" %d iterations\n", iter);
    
    calculate_stats(times, iter, size, stats);
}

/* Run decoding benchmark */
static void run_decode_benchmark(const uint8_t *digits, size_t byte_count,
                                 unsigned char *output, BenchStats *stats) {
    double times[MAX_ITERATIONS];
    int iter = 0;
    
    /* Warmup phase */
    printf("    Warmup...");
    fflush(stdout);
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        decode_simd_parallel(output, digits, byte_count);
    }
    printf(" done\n");
    
    /* Benchmark phase */
    printf("    Running iterations: ");
    fflush(stdout);
    
    double total_time = 0;
    while (iter < MIN_ITERATIONS || (total_time < TARGET_DURATION_MS && iter < MAX_ITERATIONS)) {
        uint64_t start = get_time_ns();
        decode_simd_parallel(output, digits, byte_count);
        uint64_t end = get_time_ns();
        
        double elapsed_ms = (end - start) / 1e6;
        times[iter] = elapsed_ms;
        total_time += elapsed_ms;
        iter++;
        
        printf(".");
        fflush(stdout);
    }
    printf(" %d iterations\n", iter);
    
    calculate_stats(times, iter, byte_count, stats);
}

/* Run roundtrip benchmark */
static void run_roundtrip_benchmark(const unsigned char *original, size_t size,
                                    uint8_t *encoded, unsigned char *decoded,
                                    BenchStats *stats, int *correct) {
    double times[MAX_ITERATIONS];
    int iter = 0;
    
    /* Warmup phase */
    printf("    Warmup...");
    fflush(stdout);
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        encode_simd_parallel(encoded, original, size);
        decode_simd_parallel(decoded, encoded, size);
    }
    printf(" done\n");
    
    /* Benchmark phase */
    printf("    Running iterations: ");
    fflush(stdout);
    
    double total_time = 0;
    while (iter < MIN_ITERATIONS || (total_time < TARGET_DURATION_MS && iter < MAX_ITERATIONS)) {
        uint64_t start = get_time_ns();
        encode_simd_parallel(encoded, original, size);
        decode_simd_parallel(decoded, encoded, size);
        uint64_t end = get_time_ns();
        
        double elapsed_ms = (end - start) / 1e6;
        times[iter] = elapsed_ms;
        total_time += elapsed_ms;
        iter++;
        
        printf(".");
        fflush(stdout);
    }
    printf(" %d iterations\n", iter);
    
    calculate_stats(times, iter, size, stats);
    
    /* Verify correctness */
    *correct = (memcmp(original, decoded, size) == 0) ? 1 : 0;
}

/* Print benchmark statistics */
static void print_stats(const char *label, const BenchStats *stats) {
    printf("  %s:\n", label);
    printf("    Throughput: %.2f GB/s (%.2e chars/sec)\n", 
           stats->throughput_gbps, stats->chars_per_sec);
    printf("    Latency:    min=%.3f ms, avg=%.3f ms, max=%.3f ms\n",
           stats->min, stats->avg, stats->max);
    printf("    Stddev:     %.3f ms (%.1f%%)\n",
           stats->stddev, stats->avg > 0 ? (stats->stddev / stats->avg * 100.0) : 0.0);
    printf("    Percentiles: p50=%.3f ms, p95=%.3f ms, p99=%.3f ms\n",
           stats->p50, stats->p95, stats->p99);
}

/* Run benchmark for a specific size */
static int run_benchmark_size(size_t idx) {
    const TestSize *ts = &TEST_SIZES[idx];
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BENCHMARK: %s (%zu bytes)\n", ts->name, ts->size);
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    /* Allocate buffers */
    unsigned char *input = malloc(ts->size);
    size_t output_size = ts->size * 3;
    uint8_t *encoded = malloc(output_size);
    unsigned char *decoded = malloc(ts->size);
    
    if (!input || !encoded || !decoded) {
        fprintf(stderr, "Memory allocation failed for %s\n", ts->name);
        free(input);
        free(encoded);
        free(decoded);
        return -1;
    }
    
    /* Generate test data */
    printf("  Generating %s of test data...\n", ts->name);
    for (size_t i = 0; i < ts->size; i++) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    BenchResult *res = &results[num_results];
    res->test_name = ts->name;
    res->data_size = ts->size;
    
    /* Run encode benchmark */
    printf("\n  [ENCODE]\n");
    run_encode_benchmark(input, ts->size, encoded, &res->encode_stats);
    print_stats("Encode", &res->encode_stats);
    
    /* Run decode benchmark */
    printf("\n  [DECODE]\n");
    run_decode_benchmark(encoded, ts->size, decoded, &res->decode_stats);
    print_stats("Decode", &res->decode_stats);
    
    /* Run roundtrip benchmark */
    printf("\n  [ROUNDTRIP]\n");
    run_roundtrip_benchmark(input, ts->size, encoded, decoded, 
                           &res->roundtrip_stats, &res->correctness_verified);
    print_stats("Roundtrip", &res->roundtrip_stats);
    
    printf("\n  Correctness: %s\n", 
           res->correctness_verified ? "✓ VERIFIED" : "✗ FAILED");
    
    num_results++;
    
    free(input);
    free(encoded);
    free(decoded);
    
    return res->correctness_verified ? 0 : -1;
}

/* Output results in JSON format */
static void output_json(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"benchmark\": \"Kolibri Encoding Benchmark Suite\",\n");
    fprintf(f, "  \"version\": \"1.0\",\n");
    fprintf(f, "  \"results\": [\n");
    
    for (int i = 0; i < num_results; i++) {
        BenchResult *r = &results[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"test\": \"%s\",\n", r->test_name);
        fprintf(f, "      \"data_size\": %zu,\n", r->data_size);
        fprintf(f, "      \"correctness\": %s,\n", r->correctness_verified ? "true" : "false");
        fprintf(f, "      \"encode\": {\n");
        fprintf(f, "        \"throughput_gbps\": %.4f,\n", r->encode_stats.throughput_gbps);
        fprintf(f, "        \"chars_per_sec\": %.2e,\n", r->encode_stats.chars_per_sec);
        fprintf(f, "        \"latency_ms\": {\"min\": %.3f, \"avg\": %.3f, \"max\": %.3f},\n",
                r->encode_stats.min, r->encode_stats.avg, r->encode_stats.max);
        fprintf(f, "        \"stddev_ms\": %.3f,\n", r->encode_stats.stddev);
        fprintf(f, "        \"percentiles_ms\": {\"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f},\n",
                r->encode_stats.p50, r->encode_stats.p95, r->encode_stats.p99);
        fprintf(f, "        \"iterations\": %d\n", r->encode_stats.iterations);
        fprintf(f, "      },\n");
        fprintf(f, "      \"decode\": {\n");
        fprintf(f, "        \"throughput_gbps\": %.4f,\n", r->decode_stats.throughput_gbps);
        fprintf(f, "        \"chars_per_sec\": %.2e,\n", r->decode_stats.chars_per_sec);
        fprintf(f, "        \"latency_ms\": {\"min\": %.3f, \"avg\": %.3f, \"max\": %.3f},\n",
                r->decode_stats.min, r->decode_stats.avg, r->decode_stats.max);
        fprintf(f, "        \"stddev_ms\": %.3f,\n", r->decode_stats.stddev);
        fprintf(f, "        \"percentiles_ms\": {\"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f},\n",
                r->decode_stats.p50, r->decode_stats.p95, r->decode_stats.p99);
        fprintf(f, "        \"iterations\": %d\n", r->decode_stats.iterations);
        fprintf(f, "      },\n");
        fprintf(f, "      \"roundtrip\": {\n");
        fprintf(f, "        \"throughput_gbps\": %.4f,\n", r->roundtrip_stats.throughput_gbps);
        fprintf(f, "        \"chars_per_sec\": %.2e,\n", r->roundtrip_stats.chars_per_sec);
        fprintf(f, "        \"latency_ms\": {\"min\": %.3f, \"avg\": %.3f, \"max\": %.3f},\n",
                r->roundtrip_stats.min, r->roundtrip_stats.avg, r->roundtrip_stats.max);
        fprintf(f, "        \"stddev_ms\": %.3f,\n", r->roundtrip_stats.stddev);
        fprintf(f, "        \"percentiles_ms\": {\"p50\": %.3f, \"p95\": %.3f, \"p99\": %.3f},\n",
                r->roundtrip_stats.p50, r->roundtrip_stats.p95, r->roundtrip_stats.p99);
        fprintf(f, "        \"iterations\": %d\n", r->roundtrip_stats.iterations);
        fprintf(f, "      }\n");
        fprintf(f, "    }%s\n", i < num_results - 1 ? "," : "");
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("\nJSON results written to: %s\n", filename);
}

/* Output results in Markdown format */
static void output_markdown(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }
    
    fprintf(f, "# Kolibri Encoding Benchmark Results\n\n");
    fprintf(f, "## Summary\n\n");
    fprintf(f, "| Test | Data Size | Encode (GB/s) | Decode (GB/s) | Roundtrip (GB/s) | Status |\n");
    fprintf(f, "|------|-----------|---------------|---------------|------------------|--------|\n");
    
    for (int i = 0; i < num_results; i++) {
        BenchResult *r = &results[i];
        fprintf(f, "| %s | %zu | %.2f | %.2f | %.2f | %s |\n",
                r->test_name, r->data_size,
                r->encode_stats.throughput_gbps,
                r->decode_stats.throughput_gbps,
                r->roundtrip_stats.throughput_gbps,
                r->correctness_verified ? "✓" : "✗");
    }
    
    fprintf(f, "\n## Detailed Results\n\n");
    
    for (int i = 0; i < num_results; i++) {
        BenchResult *r = &results[i];
        fprintf(f, "### %s (%zu bytes)\n\n", r->test_name, r->data_size);
        
        fprintf(f, "#### Encode Performance\n\n");
        fprintf(f, "- **Throughput:** %.2f GB/s (%.2e chars/sec)\n", 
                r->encode_stats.throughput_gbps, r->encode_stats.chars_per_sec);
        fprintf(f, "- **Latency:** min=%.3f ms, avg=%.3f ms, max=%.3f ms\n",
                r->encode_stats.min, r->encode_stats.avg, r->encode_stats.max);
        fprintf(f, "- **Percentiles:** p50=%.3f ms, p95=%.3f ms, p99=%.3f ms\n",
                r->encode_stats.p50, r->encode_stats.p95, r->encode_stats.p99);
        fprintf(f, "- **Std Dev:** %.3f ms\n\n", r->encode_stats.stddev);
        
        fprintf(f, "#### Decode Performance\n\n");
        fprintf(f, "- **Throughput:** %.2f GB/s (%.2e chars/sec)\n", 
                r->decode_stats.throughput_gbps, r->decode_stats.chars_per_sec);
        fprintf(f, "- **Latency:** min=%.3f ms, avg=%.3f ms, max=%.3f ms\n",
                r->decode_stats.min, r->decode_stats.avg, r->decode_stats.max);
        fprintf(f, "- **Percentiles:** p50=%.3f ms, p95=%.3f ms, p99=%.3f ms\n",
                r->decode_stats.p50, r->decode_stats.p95, r->decode_stats.p99);
        fprintf(f, "- **Std Dev:** %.3f ms\n\n", r->decode_stats.stddev);
        
        fprintf(f, "#### Roundtrip Performance\n\n");
        fprintf(f, "- **Throughput:** %.2f GB/s (%.2e chars/sec)\n", 
                r->roundtrip_stats.throughput_gbps, r->roundtrip_stats.chars_per_sec);
        fprintf(f, "- **Latency:** min=%.3f ms, avg=%.3f ms, max=%.3f ms\n",
                r->roundtrip_stats.min, r->roundtrip_stats.avg, r->roundtrip_stats.max);
        fprintf(f, "- **Correctness:** %s\n\n", r->correctness_verified ? "VERIFIED" : "FAILED");
    }
    
    fclose(f);
    printf("Markdown results written to: %s\n", filename);
}

/* Print usage */
static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --quick      Run quick benchmark (1KB, 1MB only)\n");
    printf("  --full       Run full benchmark (all sizes including 100MB)\n");
    printf("  --size=SIZE  Run specific size (1k, 1m, 10m, 100m)\n");
    printf("  --json=FILE  Output results to JSON file\n");
    printf("  --md=FILE    Output results to Markdown file\n");
    printf("  --help       Show this help\n");
}

int main(int argc, char **argv) {
    int quick_mode = 0;
    int full_mode = 0;
    int specific_size = -1;
    const char *json_file = NULL;
    const char *md_file = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quick") == 0) {
            quick_mode = 1;
        } else if (strcmp(argv[i], "--full") == 0) {
            full_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strncmp(argv[i], "--json=", 7) == 0) {
            json_file = argv[i] + 7;
        } else if (strncmp(argv[i], "--md=", 5) == 0) {
            md_file = argv[i] + 5;
        } else if (strncmp(argv[i], "--size=", 7) == 0) {
            const char *s = argv[i] + 7;
            if (strcmp(s, "1k") == 0) specific_size = 0;
            else if (strcmp(s, "1m") == 0) specific_size = 1;
            else if (strcmp(s, "10m") == 0) specific_size = 2;
            else if (strcmp(s, "100m") == 0) specific_size = 3;
            else {
                fprintf(stderr, "Unknown size: %s\n", s);
                return 1;
            }
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI BENCHMARK SUITE v2.0 (SIMD-Optimized)            ║\n");
    printf("║     8x Unrolled LUT + Prefetch | Parallel Decode             ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    int max_tests;
    int start_test = 0;
    
    if (specific_size >= 0) {
        start_test = specific_size;
        max_tests = specific_size + 1;
    } else if (quick_mode) {
        max_tests = 2;  /* 1KB, 1MB */
    } else if (full_mode) {
        max_tests = (int)NUM_TEST_SIZES;  /* All sizes */
    } else {
        max_tests = 3;  /* Default: 1KB, 1MB, 10MB */
    }
    
    int failed = 0;
    for (int i = start_test; i < max_tests; i++) {
        if (run_benchmark_size((size_t)i) != 0) {
            failed = 1;
        }
    }
    
    /* Print summary */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BENCHMARK SUMMARY\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("  %-8s  %12s  %12s  %12s  %8s\n", 
           "Test", "Encode", "Decode", "Roundtrip", "Status");
    printf("  %-8s  %12s  %12s  %12s  %8s\n",
           "--------", "------------", "------------", "------------", "--------");
    
    for (int i = 0; i < num_results; i++) {
        BenchResult *r = &results[i];
        printf("  %-8s  %10.2f GB/s  %10.2f GB/s  %10.2f GB/s  %8s\n",
               r->test_name,
               r->encode_stats.throughput_gbps,
               r->decode_stats.throughput_gbps,
               r->roundtrip_stats.throughput_gbps,
               r->correctness_verified ? "✓ PASS" : "✗ FAIL");
    }
    
    /* Output files */
    if (json_file) {
        output_json(json_file);
    }
    if (md_file) {
        output_markdown(md_file);
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  BENCHMARK %s\n", failed ? "COMPLETED WITH FAILURES" : "COMPLETED SUCCESSFULLY");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    return failed;
}
