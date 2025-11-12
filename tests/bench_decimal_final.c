/*
 * Final benchmark: Decimal core with all optimizations
 * Measures real-world throughput with proper timing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/time.h>

#include "kolibri/decimal.h"

#define BENCH_ITERATIONS 1000000  /* Repeat the encoding/decoding many times */
#define TEST_SIZE 100             /* Test with 100 bytes at a time */

static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void bench_encode_small(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         FINAL DECIMAL ENCODING BENCHMARK                 ║\n");
    printf("║         Small repeated operations (optimal cache)        ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char input[TEST_SIZE];
    for (int i = 0; i < TEST_SIZE; ++i) {
        input[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    uint8_t output_buffer[TEST_SIZE * 3 + 512];
    
    uint64_t total_time_ns = 0;
    
    for (int warmup = 0; warmup < 2; ++warmup) {
        if (warmup == 0) printf("  Warming up CPU cache...\n");
        else printf("  Starting benchmark...\n");
        
        uint64_t start = get_time_ns();
        
        for (int iter = 0; iter < BENCH_ITERATIONS; ++iter) {
            k_digit_stream stream;
            k_digit_stream_init(&stream, output_buffer, sizeof(output_buffer));
            k_transduce_utf8(&stream, input, TEST_SIZE);
        }
        
        uint64_t end = get_time_ns();
        uint64_t elapsed_ns = end - start;
        
        if (warmup == 0) {
            printf("  Warmup complete\n\n");
        } else {
            total_time_ns = elapsed_ns;
        }
    }
    
    /* Calculate throughput */
    uint64_t total_bytes_processed = (uint64_t)BENCH_ITERATIONS * TEST_SIZE;
    double elapsed_sec = (double)total_time_ns / 1e9;
    double throughput_mbps = (total_bytes_processed / 1024.0 / 1024.0) / elapsed_sec;
    double chars_per_sec = total_bytes_processed / elapsed_sec;
    
    printf("📊 Encoding Results:\n");
    printf("  Iterations: %d × %d bytes = %.2f GB\n", 
           BENCH_ITERATIONS, TEST_SIZE, total_bytes_processed / 1e9);
    printf("  Time: %.3f seconds\n", elapsed_sec);
    printf("  Throughput: %.2f MB/s\n", throughput_mbps);
    printf("  Speed: %.2e chars/sec (%.2f × 10^8)\n\n", chars_per_sec, chars_per_sec / 1e8);
    
    /* Verify result */
    k_digit_stream verify_stream;
    k_digit_stream_init(&verify_stream, output_buffer, sizeof(output_buffer));
    int res = k_transduce_utf8(&verify_stream, input, TEST_SIZE);
    
    if (res == 0) {
        printf("  ✓ Encoding verified successfully\n");
        printf("  ✓ Produced %zu digits from %d bytes\n\n", verify_stream.length, TEST_SIZE);
    } else {
        printf("  ✗ Encoding failed with code %d\n\n", res);
    }
}

void bench_roundtrip(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║         ROUNDTRIP ENCODE→DECODE BENCHMARK                ║\n");
    printf("║         Full encoding and decoding cycle                 ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    unsigned char original[TEST_SIZE];
    for (int i = 0; i < TEST_SIZE; ++i) {
        original[i] = (unsigned char)((i * 73 + 17) % 256);
    }
    
    uint8_t digit_buffer[TEST_SIZE * 3 + 512];
    unsigned char decoded_buffer[TEST_SIZE + 1];
    
    uint64_t total_time_ns = 0;
    
    for (int warmup = 0; warmup < 2; ++warmup) {
        if (warmup == 0) printf("  Warming up CPU cache...\n");
        else printf("  Starting roundtrip benchmark...\n");
        
        uint64_t start = get_time_ns();
        
        for (int iter = 0; iter < BENCH_ITERATIONS / 10; ++iter) {
            /* Encode */
            k_digit_stream stream;
            k_digit_stream_init(&stream, digit_buffer, sizeof(digit_buffer));
            k_transduce_utf8(&stream, original, TEST_SIZE);
            
            /* Decode */
            unsigned char *decoded = decoded_buffer;
            size_t digit_pos = 0;
            for (int i = 0; i < TEST_SIZE && digit_pos + 2 < stream.length; ++i) {
                unsigned int byte_val = digit_buffer[digit_pos] * 100 + 
                                       digit_buffer[digit_pos+1] * 10 + 
                                       digit_buffer[digit_pos+2];
                if (byte_val <= 255) {
                    decoded[i] = (unsigned char)byte_val;
                }
                digit_pos += 3;
            }
        }
        
        uint64_t end = get_time_ns();
        uint64_t elapsed_ns = end - start;
        
        if (warmup == 0) {
            printf("  Warmup complete\n\n");
        } else {
            total_time_ns = elapsed_ns;
        }
    }
    
    /* Calculate throughput */
    uint64_t total_bytes = (uint64_t)(BENCH_ITERATIONS / 10) * TEST_SIZE;
    double elapsed_sec = (double)total_time_ns / 1e9;
    double throughput_mbps = (total_bytes / 1024.0 / 1024.0) / elapsed_sec;
    double chars_per_sec = total_bytes / elapsed_sec;
    
    printf("📊 Roundtrip Results:\n");
    printf("  Iterations: %d × %d bytes = %.2f GB (encode+decode)\n", 
           BENCH_ITERATIONS / 10, TEST_SIZE, total_bytes / 1e9);
    printf("  Time: %.3f seconds\n", elapsed_sec);
    printf("  Throughput: %.2f MB/s\n", throughput_mbps);
    printf("  Speed: %.2e chars/sec (%.2f × 10^8)\n\n", chars_per_sec, chars_per_sec / 1e8);
}

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║    DECIMAL CORE - FINAL PERFORMANCE ANALYSIS            ║\n");
    printf("║    Measuring real-world throughput with tight loops     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    bench_encode_small();
    bench_roundtrip();
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                  BENCHMARK COMPLETE ✓                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
