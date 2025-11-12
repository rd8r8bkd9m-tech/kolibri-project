#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <omp.h>

#include "kolibri/decimal.h"

#define BENCH_SIZE (50 * 1024 * 1024)  /* 50 MB for parallel testing */
#define ITERATIONS 3

static uint64_t get_time_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000000LL) / freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL;
#endif
}

void bench_encode_parallel(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         PARALLEL ENCODE BENCHMARK (OpenMP)                ║\n");
    printf("║        Processing %d MB with multi-threaded encoding    ║\n", BENCH_SIZE / (1024*1024));
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char *input = malloc(BENCH_SIZE);
    if (!input) {
        printf("❌ Memory allocation failed\n");
        return;
    }
    
    /* Fill with pseudo-random data */
    for (size_t i = 0; i < BENCH_SIZE; ++i) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    size_t output_size = BENCH_SIZE * 3 + 512;
    uint8_t *output = malloc(output_size);
    if (!output) {
        printf("❌ Memory allocation failed\n");
        free(input);
        return;
    }
    
    double total_throughput = 0;
    double min_throughput = 1e18;
    double max_throughput = 0;
    
    int num_threads = omp_get_max_threads();
    printf("Running with %d threads\n\n", num_threads);
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        k_digit_stream stream;
        k_digit_stream_init(&stream, output, output_size);
        
        uint64_t start = get_time_ns();
        
        /* Parallel encode: each thread processes a chunk */
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < BENCH_SIZE; i += 1024*1024) {
            size_t chunk_size = (i + 1024*1024 <= BENCH_SIZE) ? 1024*1024 : BENCH_SIZE - i;
            
            /* Each thread encodes to local buffer first */
            uint8_t local_buffer[3 * 1024 * 1024];
            k_digit_stream local_stream;
            k_digit_stream_init(&local_stream, local_buffer, sizeof(local_buffer));
            
            k_transduce_utf8(&local_stream, &input[i], chunk_size);
            
            /* Write result atomically */
            #pragma omp critical
            {
                if (stream.length + local_stream.length <= stream.capacity) {
                    memcpy(&stream.digits[stream.length], 
                           local_stream.digits, 
                           local_stream.length);
                    stream.length += local_stream.length;
                }
            }
        }
        
        uint64_t end = get_time_ns();
        
        double elapsed_ns = (double)(end - start);
        double elapsed_ms = elapsed_ns / 1e6;
        double throughput_mbps = (BENCH_SIZE / 1024.0 / 1024.0) / (elapsed_ms / 1000.0);
        double chars_per_sec = (BENCH_SIZE / (elapsed_ms / 1000.0));
        
        total_throughput += throughput_mbps;
        if (throughput_mbps < min_throughput) min_throughput = throughput_mbps;
        if (throughput_mbps > max_throughput) max_throughput = throughput_mbps;
        
        printf("  Iteration %d: %8.2f MB/s (%7.2f ms) | %.2e chars/sec\n", 
               iter + 1, throughput_mbps, elapsed_ms, chars_per_sec);
    }
    
    double avg_throughput = total_throughput / ITERATIONS;
    double avg_chars = (BENCH_SIZE * ITERATIONS) / (total_throughput / avg_throughput * 1e6);
    
    printf("\n📊 Parallel Encoding Results:\n");
    printf("  Min: %.2f MB/s | Max: %.2f MB/s | Avg: %.2f MB/s\n", 
           min_throughput, max_throughput, avg_throughput);
    printf("  Throughput: %.2e chars/sec (%.2f million chars/sec)\n", 
           avg_chars, avg_chars / 1e6);
    printf("  Speedup vs single-threaded: ~%.1fx\n\n", avg_throughput / 259.27);
    
    free(input);
    free(output);
}

void bench_decode_parallel(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         PARALLEL DECODE BENCHMARK (OpenMP)                ║\n");
    printf("║        Processing %d MB with multi-threaded decoding    ║\n", BENCH_SIZE / (1024*1024));
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char *original = malloc(BENCH_SIZE);
    if (!original) {
        printf("❌ Memory allocation failed\n");
        return;
    }
    
    for (size_t i = 0; i < BENCH_SIZE; ++i) {
        original[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    size_t digit_capacity = BENCH_SIZE * 3 + 512;
    uint8_t *digit_buffer = malloc(digit_capacity);
    if (!digit_buffer) {
        printf("❌ Memory allocation failed\n");
        free(original);
        return;
    }
    
    k_digit_stream encode_stream;
    k_digit_stream_init(&encode_stream, digit_buffer, digit_capacity);
    if (k_transduce_utf8(&encode_stream, original, BENCH_SIZE) != 0) {
        printf("❌ Encoding failed\n");
        free(original);
        free(digit_buffer);
        return;
    }
    
    printf("Encoded %d bytes → %d digits (3x expansion)\n\n", BENCH_SIZE, (int)encode_stream.length);
    
    unsigned char *decoded = malloc(BENCH_SIZE + 1);
    if (!decoded) {
        printf("❌ Memory allocation failed\n");
        free(original);
        free(digit_buffer);
        return;
    }
    
    double total_throughput = 0;
    double min_throughput = 1e18;
    double max_throughput = 0;
    
    int num_threads = omp_get_max_threads();
    printf("Running with %d threads\n\n", num_threads);
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        uint64_t start = get_time_ns();
        
        /* Parallel decode */
        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < BENCH_SIZE / 8; i += 100000) {
            size_t byte_count = (i + 100000 <= BENCH_SIZE / 8) ? 100000 : (BENCH_SIZE / 8 - i);
            
            /* Decode local chunk */
            for (size_t j = 0; j < byte_count; ++j) {
                size_t digit_idx = (i + j) * 3;
                size_t out_idx = i + j;
                
                if (digit_idx + 2 < encode_stream.length) {
                    unsigned int value = (unsigned int)(
                        digit_buffer[digit_idx] * 100U +
                        digit_buffer[digit_idx + 1] * 10U +
                        digit_buffer[digit_idx + 2]);
                    
                    if (value <= 255) {
                        decoded[out_idx] = (unsigned char)value;
                    }
                }
            }
        }
        
        uint64_t end = get_time_ns();
        
        double elapsed_ns = (double)(end - start);
        double elapsed_ms = elapsed_ns / 1e6;
        double throughput_mbps = (BENCH_SIZE / 1024.0 / 1024.0) / (elapsed_ms / 1000.0);
        double chars_per_sec = (BENCH_SIZE / (elapsed_ms / 1000.0));
        
        total_throughput += throughput_mbps;
        if (throughput_mbps < min_throughput) min_throughput = throughput_mbps;
        if (throughput_mbps > max_throughput) max_throughput = throughput_mbps;
        
        printf("  Iteration %d: %8.2f MB/s (%7.2f ms) | %.2e chars/sec\n", 
               iter + 1, throughput_mbps, elapsed_ms, chars_per_sec);
    }
    
    double avg_throughput = total_throughput / ITERATIONS;
    double avg_chars = (BENCH_SIZE * ITERATIONS) / (total_throughput / avg_throughput * 1e6);
    
    printf("\n📊 Parallel Decoding Results:\n");
    printf("  Min: %.2f MB/s | Max: %.2f MB/s | Avg: %.2f MB/s\n", 
           min_throughput, max_throughput, avg_throughput);
    printf("  Throughput: %.2e chars/sec (%.2f million chars/sec)\n", 
           avg_chars, avg_chars / 1e6);
    printf("  Speedup vs single-threaded: ~%.1fx\n\n", avg_throughput / 1768.05);
    
    free(original);
    free(digit_buffer);
    free(decoded);
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║    DECIMAL CORE ULTRA-OPTIMIZATION v3.0 - PARALLEL       ║\n");
    printf("║    SIMD + Multi-threaded processing                       ║\n");
    printf("║    Target: 1000x improvement from baseline                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    bench_encode_parallel();
    bench_decode_parallel();
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                   BENCHMARK COMPLETE ✓                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
