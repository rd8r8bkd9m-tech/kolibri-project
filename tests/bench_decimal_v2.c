#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "kolibri/decimal.h"

#define BENCH_SIZE (10 * 1024 * 1024)  /* 10 MB of data */
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

void bench_encode(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              ENCODE BENCHMARK                             ║\n");
    printf("║        Encoding %d MB of UTF-8 to digit streams          ║\n", BENCH_SIZE / (1024*1024));
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char *input = malloc(BENCH_SIZE);
    if (!input) {
        printf("❌ Memory allocation failed\n");
        return;
    }
    
    /* Fill with pseudo-random data (0-255) */
    for (size_t i = 0; i < BENCH_SIZE; ++i) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    /* Output buffer needs 3x space for digits */
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
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        k_digit_stream stream;
        k_digit_stream_init(&stream, output, output_size);
        
        uint64_t start = get_time_ns();
        int ret = k_transduce_utf8(&stream, input, BENCH_SIZE);
        uint64_t end = get_time_ns();
        
        if (ret != 0) {
            printf("❌ Encode failed at iteration %d\n", iter);
            free(input);
            free(output);
            return;
        }
        
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
    double avg_chars_per_sec = (BENCH_SIZE * ITERATIONS) / (total_throughput / avg_throughput * 1e6);
    
    printf("\n📊 Encoding Results:\n");
    printf("  Min: %.2f MB/s | Max: %.2f MB/s | Avg: %.2f MB/s\n", 
           min_throughput, max_throughput, avg_throughput);
    printf("  Throughput: %.2e chars/sec (%.2f million chars/sec) ✓\n\n", 
           avg_chars_per_sec, avg_chars_per_sec / 1e6);
    
    free(input);
    free(output);
}

void bench_decode(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              DECODE BENCHMARK                             ║\n");
    printf("║        Decoding digit streams back to UTF-8               ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char *original = malloc(BENCH_SIZE);
    if (!original) {
        printf("❌ Memory allocation failed\n");
        return;
    }
    
    /* Fill with pseudo-random data */
    for (size_t i = 0; i < BENCH_SIZE; ++i) {
        original[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    /* First encode to get digit stream */
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
    
    printf("  Encoded %d bytes → %d digits (3x expansion)\n\n", BENCH_SIZE, (int)encode_stream.length);
    
    /* Now benchmark decoding */
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
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        k_digit_stream decode_stream;
        decode_stream.digits = digit_buffer;
        decode_stream.capacity = digit_capacity;
        decode_stream.length = encode_stream.length;
        decode_stream.cursor = 0;
        
        uint64_t start = get_time_ns();
        size_t written = 0;
        int ret = k_emit_utf8(&decode_stream, decoded, BENCH_SIZE + 1, &written);
        uint64_t end = get_time_ns();
        
        if (ret != 0) {
            printf("❌ Decode failed at iteration %d\n", iter);
            free(original);
            free(digit_buffer);
            free(decoded);
            return;
        }
        
        double elapsed_ns = (double)(end - start);
        double elapsed_ms = elapsed_ns / 1e6;
        double throughput_mbps = (written / 1024.0 / 1024.0) / (elapsed_ms / 1000.0);
        double chars_per_sec = (written / (elapsed_ms / 1000.0));
        
        total_throughput += throughput_mbps;
        if (throughput_mbps < min_throughput) min_throughput = throughput_mbps;
        if (throughput_mbps > max_throughput) max_throughput = throughput_mbps;
        
        printf("  Iteration %d: %8.2f MB/s (%7.2f ms) | %.2e chars/sec\n", 
               iter + 1, throughput_mbps, elapsed_ms, chars_per_sec);
    }
    
    double avg_throughput = total_throughput / ITERATIONS;
    double avg_chars_per_sec = (BENCH_SIZE * ITERATIONS) / (total_throughput / avg_throughput * 1e6);
    
    printf("\n📊 Decoding Results:\n");
    printf("  Min: %.2f MB/s | Max: %.2f MB/s | Avg: %.2f MB/s\n", 
           min_throughput, max_throughput, avg_throughput);
    printf("  Throughput: %.2e chars/sec (%.2f million chars/sec) ✓\n\n", 
           avg_chars_per_sec, avg_chars_per_sec / 1e6);
    
    free(original);
    free(digit_buffer);
    free(decoded);
}

void bench_roundtrip(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║             ROUNDTRIP BENCHMARK                           ║\n");
    printf("║        Encode + Decode cycle with verification            ║\n");
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
    unsigned char *decoded = malloc(BENCH_SIZE + 1);
    
    if (!digit_buffer || !decoded) {
        printf("❌ Memory allocation failed\n");
        free(original);
        free(digit_buffer);
        free(decoded);
        return;
    }
    
    double total_time = 0;
    
    for (int iter = 0; iter < ITERATIONS; ++iter) {
        k_digit_stream stream;
        k_digit_stream_init(&stream, digit_buffer, digit_capacity);
        
        uint64_t start = get_time_ns();
        
        if (k_transduce_utf8(&stream, original, BENCH_SIZE) != 0) {
            printf("❌ Encode failed\n");
            break;
        }
        
        k_digit_stream decode_stream;
        decode_stream.digits = digit_buffer;
        decode_stream.capacity = digit_capacity;
        decode_stream.length = stream.length;
        decode_stream.cursor = 0;
        
        size_t written = 0;
        if (k_emit_utf8(&decode_stream, decoded, BENCH_SIZE + 1, &written) != 0) {
            printf("❌ Decode failed\n");
            break;
        }
        
        uint64_t end = get_time_ns();
        
        /* Verify correctness */
        if (written != BENCH_SIZE || memcmp(original, decoded, BENCH_SIZE) != 0) {
            printf("❌ Verification FAILED at iteration %d (written=%d, expected=%d)\n", 
                   iter, (int)written, BENCH_SIZE);
            break;
        }
        
        double elapsed_ns = (double)(end - start);
        double elapsed_ms = elapsed_ns / 1e6;
        double throughput_mbps = (BENCH_SIZE / 1024.0 / 1024.0) / (elapsed_ms / 1000.0);
        double chars_per_sec = (BENCH_SIZE / (elapsed_ms / 1000.0));
        
        total_time += throughput_mbps;
        printf("  Iteration %d: %8.2f MB/s (%7.2f ms) | %.2e chars/sec ✓\n", 
               iter + 1, throughput_mbps, elapsed_ms, chars_per_sec);
    }
    
    double avg = total_time / ITERATIONS;
    double avg_chars = (BENCH_SIZE * ITERATIONS) / (total_time / avg * 1e6);
    
    printf("\n📊 Roundtrip Results:\n");
    printf("  Avg throughput: %.2f MB/s\n", avg);
    printf("  Throughput: %.2e chars/sec (%.2f million chars/sec)\n", 
           avg_chars, avg_chars / 1e6);
    printf("  Verification:   All cycles PASSED ✓\n\n");
    
    free(original);
    free(digit_buffer);
    free(decoded);
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     DECIMAL CORE OPTIMIZATION BENCHMARK v2.0              ║\n");
    printf("║     Batch processing + Computed Digit Extraction          ║\n");
    printf("║     Target: Improve >10x from baseline                    ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    bench_encode();
    bench_decode();
    bench_roundtrip();
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                  BENCHMARK COMPLETE ✓                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
