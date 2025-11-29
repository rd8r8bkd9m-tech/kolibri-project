/*
 * Kolibri vs Competitors Benchmark
 * Compare Kolibri encoding with Base64, Hex, and other encodings
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

/* Benchmark configuration */
#define BENCH_SIZE (10 * 1024 * 1024)  /* 10 MB */
#define ITERATIONS 5
#define WARMUP_ITERATIONS 2

/* Result structure */
typedef struct {
    const char *name;
    double encode_gbps;
    double decode_gbps;
    double encode_chars_per_sec;
    double decode_chars_per_sec;
    double expansion_ratio;
} CompetitorResult;

static CompetitorResult competitor_results[10];
static int num_competitors = 0;

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

/* ============================================== */
/*           KOLIBRI ENCODING (LUT)              */
/* ============================================== */

/* Pre-computed lookup table for Kolibri encoding */
static const uint8_t KOLIBRI_LUT[256][3] = {
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

static void kolibri_encode(uint8_t *out, const unsigned char *in, size_t len) {
    size_t out_pos = 0;
    for (size_t i = 0; i < len; i++) {
        const uint8_t *d = KOLIBRI_LUT[in[i]];
        out[out_pos++] = d[0] + '0';
        out[out_pos++] = d[1] + '0';
        out[out_pos++] = d[2] + '0';
    }
}

static void kolibri_decode(unsigned char *out, const uint8_t *digits, size_t byte_count) {
    for (size_t i = 0; i < byte_count; i++) {
        size_t offset = i * 3;
        unsigned int value = (digits[offset] - '0') * 100U + 
                            (digits[offset + 1] - '0') * 10U + 
                            (digits[offset + 2] - '0');
        out[i] = (unsigned char)value;
    }
}

/* ============================================== */
/*           BASE64 ENCODING                     */
/* ============================================== */

static const char base64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const int base64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static size_t base64_encode(char *out, const unsigned char *in, size_t len) {
    size_t i, j;
    for (i = 0, j = 0; i + 2 < len; i += 3) {
        out[j++] = base64_table[(in[i] >> 2) & 0x3F];
        out[j++] = base64_table[((in[i] & 0x03) << 4) | ((in[i + 1] >> 4) & 0x0F)];
        out[j++] = base64_table[((in[i + 1] & 0x0F) << 2) | ((in[i + 2] >> 6) & 0x03)];
        out[j++] = base64_table[in[i + 2] & 0x3F];
    }
    if (i < len) {
        out[j++] = base64_table[(in[i] >> 2) & 0x3F];
        if (i + 1 < len) {
            out[j++] = base64_table[((in[i] & 0x03) << 4) | ((in[i + 1] >> 4) & 0x0F)];
            out[j++] = base64_table[(in[i + 1] & 0x0F) << 2];
        } else {
            out[j++] = base64_table[(in[i] & 0x03) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    return j;
}

static size_t base64_decode(unsigned char *out, const char *in, size_t len) {
    size_t i, j;
    unsigned int v;
    for (i = 0, j = 0; i < len; ) {
        v = 0;
        int k;
        for (k = 0; k < 4 && i < len; k++) {
            if (in[i] == '=') {
                i++;
                continue;
            }
            int c = base64_decode_table[(unsigned char)in[i++]];
            if (c >= 0) {
                v = (v << 6) | (unsigned int)c;
            }
        }
        if (k >= 2) out[j++] = (unsigned char)((v >> 16) & 0xFF);
        if (k >= 3) out[j++] = (unsigned char)((v >> 8) & 0xFF);
        if (k >= 4) out[j++] = (unsigned char)(v & 0xFF);
    }
    return j;
}

/* ============================================== */
/*           HEX ENCODING                        */
/* ============================================== */

static const char hex_table[] = "0123456789abcdef";
static const int hex_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static void hex_encode(char *out, const unsigned char *in, size_t len) {
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex_table[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex_table[in[i] & 0x0F];
    }
}

static size_t hex_decode(unsigned char *out, const char *in, size_t len) {
    size_t j = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        int high = hex_decode_table[(unsigned char)in[i]];
        int low = hex_decode_table[(unsigned char)in[i + 1]];
        if (high >= 0 && low >= 0) {
            out[j++] = (unsigned char)((high << 4) | low);
        }
    }
    return j;
}

/* ============================================== */
/*           BENCHMARK RUNNER                    */
/* ============================================== */

static void run_competitor_benchmark(
    const char *name,
    const unsigned char *input,
    size_t input_size,
    void (*encode_fn)(void*, const void*, size_t),
    size_t (*decode_fn)(void*, const void*, size_t),
    size_t encoded_size
) {
    void *encoded = malloc(encoded_size + 1);
    void *decoded = malloc(input_size + 1);
    
    if (!encoded || !decoded) {
        fprintf(stderr, "Memory allocation failed for %s\n", name);
        free(encoded);
        free(decoded);
        return;
    }
    
    printf("  Testing %s...\n", name);
    
    /* Warmup */
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        encode_fn(encoded, input, input_size);
    }
    
    /* Encode benchmark */
    double encode_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        encode_fn(encoded, input, input_size);
        uint64_t end = get_time_ns();
        encode_total += (end - start) / 1e9;
    }
    double encode_avg = encode_total / ITERATIONS;
    double encode_gbps = (input_size / (1024.0 * 1024.0 * 1024.0)) / encode_avg;
    double encode_chars = input_size / encode_avg;
    
    /* Decode warmup */
    for (int w = 0; w < WARMUP_ITERATIONS; w++) {
        decode_fn(decoded, encoded, encoded_size);
    }
    
    /* Decode benchmark */
    double decode_total = 0;
    for (int iter = 0; iter < ITERATIONS; iter++) {
        uint64_t start = get_time_ns();
        decode_fn(decoded, encoded, encoded_size);
        uint64_t end = get_time_ns();
        decode_total += (end - start) / 1e9;
    }
    double decode_avg = decode_total / ITERATIONS;
    double decode_gbps = (input_size / (1024.0 * 1024.0 * 1024.0)) / decode_avg;
    double decode_chars = input_size / decode_avg;
    
    /* Store results */
    CompetitorResult *res = &competitor_results[num_competitors++];
    res->name = name;
    res->encode_gbps = encode_gbps;
    res->decode_gbps = decode_gbps;
    res->encode_chars_per_sec = encode_chars;
    res->decode_chars_per_sec = decode_chars;
    res->expansion_ratio = (double)encoded_size / input_size;
    
    printf("    Encode: %.2f GB/s | Decode: %.2f GB/s | Expansion: %.2fx\n",
           encode_gbps, decode_gbps, res->expansion_ratio);
    
    free(encoded);
    free(decoded);
}

/* Wrapper functions for uniform interface */
static void kolibri_encode_wrapper(void *out, const void *in, size_t len) {
    kolibri_encode((uint8_t *)out, (const unsigned char *)in, len);
}

static size_t kolibri_decode_wrapper(void *out, const void *in, size_t len) {
    kolibri_decode((unsigned char *)out, (const uint8_t *)in, len / 3);
    return len / 3;
}

static void base64_encode_wrapper(void *out, const void *in, size_t len) {
    base64_encode((char *)out, (const unsigned char *)in, len);
}

static size_t base64_decode_wrapper(void *out, const void *in, size_t len) {
    return base64_decode((unsigned char *)out, (const char *)in, len);
}

static void hex_encode_wrapper(void *out, const void *in, size_t len) {
    hex_encode((char *)out, (const unsigned char *)in, len);
}

static size_t hex_decode_wrapper(void *out, const void *in, size_t len) {
    return hex_decode((unsigned char *)out, (const char *)in, len);
}

/* Output comparison table */
static void output_comparison_table(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                        COMPARISON TABLE                                    ║\n");
    printf("╠════════════════════════════════════════════════════════════════════════════╣\n");
    printf("║ %-12s │ %12s │ %12s │ %12s │ %8s ║\n",
           "Encoding", "Encode GB/s", "Decode GB/s", "Chars/sec", "Expansion");
    printf("╠══════════════╪══════════════╪══════════════╪══════════════╪══════════╣\n");
    
    for (int i = 0; i < num_competitors; i++) {
        CompetitorResult *r = &competitor_results[i];
        printf("║ %-12s │ %10.2f   │ %10.2f   │ %10.2e   │ %6.2fx  ║\n",
               r->name, r->encode_gbps, r->decode_gbps, 
               r->encode_chars_per_sec, r->expansion_ratio);
    }
    
    printf("╚══════════════╧══════════════╧══════════════╧══════════════╧══════════╝\n");
    
    /* Calculate speedup relative to Base64 */
    if (num_competitors >= 2) {
        double base64_encode = competitor_results[1].encode_gbps;
        double kolibri_encode = competitor_results[0].encode_gbps;
        double speedup = kolibri_encode / base64_encode;
        
        printf("\n  Kolibri vs Base64 speedup: %.1fx\n", speedup);
    }
}

/* Output JSON results */
static void output_json(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"benchmark\": \"Kolibri vs Competitors\",\n");
    fprintf(f, "  \"data_size\": %d,\n", BENCH_SIZE);
    fprintf(f, "  \"results\": [\n");
    
    for (int i = 0; i < num_competitors; i++) {
        CompetitorResult *r = &competitor_results[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"name\": \"%s\",\n", r->name);
        fprintf(f, "      \"encode_gbps\": %.4f,\n", r->encode_gbps);
        fprintf(f, "      \"decode_gbps\": %.4f,\n", r->decode_gbps);
        fprintf(f, "      \"encode_chars_per_sec\": %.2e,\n", r->encode_chars_per_sec);
        fprintf(f, "      \"decode_chars_per_sec\": %.2e,\n", r->decode_chars_per_sec);
        fprintf(f, "      \"expansion_ratio\": %.4f\n", r->expansion_ratio);
        fprintf(f, "    }%s\n", i < num_competitors - 1 ? "," : "");
    }
    
    fprintf(f, "  ]\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("\nJSON results written to: %s\n", filename);
}

int main(int argc, char **argv) {
    const char *json_file = NULL;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--json=", 7) == 0) {
            json_file = argv[i] + 7;
        }
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI vs COMPETITORS BENCHMARK                                      ║\n");
    printf("║     Comparing encoding performance with Base64, Hex, etc.                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n\n");
    
    printf("  Test data size: %d MB\n", BENCH_SIZE / (1024 * 1024));
    printf("  Iterations: %d (with %d warmup)\n\n", ITERATIONS, WARMUP_ITERATIONS);
    
    /* Generate test data */
    printf("  Generating test data...\n");
    unsigned char *input = malloc(BENCH_SIZE);
    if (!input) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    
    for (size_t i = 0; i < BENCH_SIZE; i++) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    printf("\n  Running benchmarks:\n\n");
    
    /* Kolibri (3x expansion: 1 byte -> 3 digits) */
    run_competitor_benchmark(
        "Kolibri",
        input, BENCH_SIZE,
        kolibri_encode_wrapper,
        kolibri_decode_wrapper,
        BENCH_SIZE * 3
    );
    
    /* Base64 (4/3 expansion: 3 bytes -> 4 chars) */
    run_competitor_benchmark(
        "Base64",
        input, BENCH_SIZE,
        base64_encode_wrapper,
        base64_decode_wrapper,
        (BENCH_SIZE + 2) / 3 * 4
    );
    
    /* Hex (2x expansion: 1 byte -> 2 chars) */
    run_competitor_benchmark(
        "Hex",
        input, BENCH_SIZE,
        hex_encode_wrapper,
        hex_decode_wrapper,
        BENCH_SIZE * 2
    );
    
    /* Output comparison table */
    output_comparison_table();
    
    /* Output JSON if requested */
    if (json_file) {
        output_json(json_file);
    }
    
    free(input);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n");
    printf("  BENCHMARK COMPLETE\n");
    printf("═══════════════════════════════════════════════════════════════════════════════\n\n");
    
    return 0;
}
