/*
 * Kolibri Stress Tests
 * Multi-threaded stress testing with memory and correctness verification
 * 
 * Copyright (c) 2025 Kolibri Project
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include "kolibri/decimal.h"

/* Test configuration */
#define DEFAULT_NUM_THREADS 8
#define DEFAULT_DURATION_SEC 60
#define CHUNK_SIZE (1024 * 1024)  /* 1 MB per operation */
#define MAX_CHUNK_SIZE (10 * 1024 * 1024)  /* 10 MB max */

/* Global state */
static volatile int running = 1;

/* Thread statistics */
typedef struct {
    uint64_t operations;
    uint64_t bytes_processed;
    uint64_t errors;
    uint64_t verification_failures;
} ThreadStats;

/* Global statistics */
static ThreadStats global_stats = {0};

/* Get current time in seconds */
static double get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

/* Random number generator (thread-safe) */
static unsigned int rand_state = 12345;
static pthread_mutex_t rand_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int thread_rand(void) {
    pthread_mutex_lock(&rand_mutex);
    rand_state = rand_state * 1103515245 + 12345;
    unsigned int result = (rand_state / 65536) % 32768;
    pthread_mutex_unlock(&rand_mutex);
    return result;
}

/* ============================================== */
/*           STRESS TEST THREAD                  */
/* ============================================== */

typedef struct {
    int thread_id;
    size_t chunk_size;
    ThreadStats stats;
} ThreadContext;

static void *stress_thread(void *arg) {
    ThreadContext *ctx = (ThreadContext *)arg;
    
    /* Allocate buffers */
    unsigned char *input = malloc(ctx->chunk_size);
    uint8_t *encoded = malloc(ctx->chunk_size * 3);
    unsigned char *decoded = malloc(ctx->chunk_size);
    
    if (!input || !encoded || !decoded) {
        fprintf(stderr, "Thread %d: Memory allocation failed\n", ctx->thread_id);
        ctx->stats.errors++;
        free(input);
        free(encoded);
        free(decoded);
        return NULL;
    }
    
    while (running) {
        /* Generate random data */
        unsigned int seed = thread_rand();
        for (size_t i = 0; i < ctx->chunk_size; i++) {
            input[i] = (unsigned char)((i * seed + 17) % 256);
        }
        
        /* Encode */
        k_digit_stream stream;
        k_digit_stream_init(&stream, encoded, ctx->chunk_size * 3);
        
        if (k_transduce_utf8(&stream, input, ctx->chunk_size) != 0) {
            ctx->stats.errors++;
            continue;
        }
        
        /* Decode */
        size_t written = 0;
        if (k_emit_utf8(&stream, decoded, ctx->chunk_size, &written) != 0) {
            ctx->stats.errors++;
            continue;
        }
        
        /* Verify */
        if (written != ctx->chunk_size || memcmp(input, decoded, ctx->chunk_size) != 0) {
            ctx->stats.verification_failures++;
            continue;
        }
        
        /* Update stats */
        ctx->stats.operations++;
        ctx->stats.bytes_processed += ctx->chunk_size;
    }
    
    /* Cleanup */
    free(input);
    free(encoded);
    free(decoded);
    
    return NULL;
}

/* ============================================== */
/*           RANDOM DATA STRESS TEST             */
/* ============================================== */

typedef struct {
    int thread_id;
    ThreadStats stats;
} RandomThreadContext;

static void *random_stress_thread(void *arg) {
    RandomThreadContext *ctx = (RandomThreadContext *)arg;
    
    while (running) {
        /* Random chunk size between 1KB and 10MB */
        size_t size = (thread_rand() % MAX_CHUNK_SIZE) + 1024;
        
        /* Allocate buffers */
        unsigned char *input = malloc(size);
        uint8_t *encoded = malloc(size * 3);
        unsigned char *decoded = malloc(size);
        
        if (!input || !encoded || !decoded) {
            free(input);
            free(encoded);
            free(decoded);
            ctx->stats.errors++;
            continue;
        }
        
        /* Fill with truly random data */
        for (size_t i = 0; i < size; i++) {
            input[i] = (unsigned char)(thread_rand() % 256);
        }
        
        /* Encode */
        k_digit_stream stream;
        k_digit_stream_init(&stream, encoded, size * 3);
        
        int encode_ok = (k_transduce_utf8(&stream, input, size) == 0);
        
        /* Decode */
        size_t written = 0;
        int decode_ok = encode_ok && (k_emit_utf8(&stream, decoded, size, &written) == 0);
        
        /* Verify */
        int verify_ok = decode_ok && 
                        (written == size) && 
                        (memcmp(input, decoded, size) == 0);
        
        if (!encode_ok || !decode_ok) {
            ctx->stats.errors++;
        } else if (!verify_ok) {
            ctx->stats.verification_failures++;
        } else {
            ctx->stats.operations++;
            ctx->stats.bytes_processed += size;
        }
        
        free(input);
        free(encoded);
        free(decoded);
    }
    
    return NULL;
}

/* ============================================== */
/*           MEMORY LEAK TEST                    */
/* ============================================== */

static int test_memory_leaks(void) {
    printf("  Memory leak test...\n");
    
    const int iterations = 10000;
    int errors = 0;
    
    for (int i = 0; i < iterations && running; i++) {
        /* Allocate buffers */
        size_t size = 1024;
        unsigned char *input = malloc(size);
        uint8_t *encoded = malloc(size * 3);
        unsigned char *decoded = malloc(size);
        
        if (!input || !encoded || !decoded) {
            errors++;
            free(input);
            free(encoded);
            free(decoded);
            continue;
        }
        
        /* Fill with data */
        memset(input, (unsigned char)(i % 256), size);
        
        /* Encode/decode cycle */
        k_digit_stream stream;
        k_digit_stream_init(&stream, encoded, size * 3);
        k_transduce_utf8(&stream, input, size);
        
        size_t written = 0;
        k_emit_utf8(&stream, decoded, size, &written);
        
        /* Verify */
        if (written != size || memcmp(input, decoded, size) != 0) {
            errors++;
        }
        
        /* Free all allocations */
        free(input);
        free(encoded);
        free(decoded);
        
        if ((i + 1) % 1000 == 0) {
            printf("    %d/%d iterations completed\n", i + 1, iterations);
        }
    }
    
    printf("    Memory leak test complete: %d errors\n", errors);
    return errors == 0;
}

/* ============================================== */
/*           RUN STRESS TEST                     */
/* ============================================== */

static void run_stress_test(int num_threads, int duration_sec) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI STRESS TEST                                       ║\n");
    printf("║     Threads: %d | Duration: %d seconds                         ║\n",
           num_threads, duration_sec);
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    /* Create thread contexts */
    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    ThreadContext *contexts = malloc((size_t)num_threads * sizeof(ThreadContext));
    
    if (!threads || !contexts) {
        fprintf(stderr, "Failed to allocate thread structures\n");
        free(threads);
        free(contexts);
        return;
    }
    
    /* Initialize contexts */
    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        contexts[i].chunk_size = CHUNK_SIZE;
        memset(&contexts[i].stats, 0, sizeof(ThreadStats));
    }
    
    /* Start threads */
    printf("  Starting %d threads...\n", num_threads);
    double start_time = get_time_sec();
    
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, stress_thread, &contexts[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            contexts[i].stats.errors++;
        }
    }
    
    /* Progress reporting */
    int elapsed = 0;
    while (elapsed < duration_sec && running) {
        sleep(1);
        elapsed++;
        
        /* Calculate current stats */
        uint64_t total_ops = 0;
        uint64_t total_bytes = 0;
        uint64_t total_errors = 0;
        
        for (int i = 0; i < num_threads; i++) {
            total_ops += contexts[i].stats.operations;
            total_bytes += contexts[i].stats.bytes_processed;
            total_errors += contexts[i].stats.errors + contexts[i].stats.verification_failures;
        }
        
        double throughput = total_bytes / (double)(elapsed) / (1024 * 1024);
        
        printf("  [%3d/%d sec] Ops: %lu | Throughput: %.2f MB/s | Errors: %lu\n",
               elapsed, duration_sec, 
               (unsigned long)total_ops, 
               throughput, 
               (unsigned long)total_errors);
    }
    
    /* Signal threads to stop */
    running = 0;
    
    /* Wait for threads to finish */
    printf("\n  Waiting for threads to finish...\n");
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = get_time_sec() - start_time;
    
    /* Aggregate statistics */
    for (int i = 0; i < num_threads; i++) {
        global_stats.operations += contexts[i].stats.operations;
        global_stats.bytes_processed += contexts[i].stats.bytes_processed;
        global_stats.errors += contexts[i].stats.errors;
        global_stats.verification_failures += contexts[i].stats.verification_failures;
    }
    
    /* Print results */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  STRESS TEST RESULTS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("  Duration:      %.2f seconds\n", total_time);
    printf("  Threads:       %d\n", num_threads);
    printf("  Operations:    %lu\n", (unsigned long)global_stats.operations);
    printf("  Bytes:         %lu MB\n", 
           (unsigned long)(global_stats.bytes_processed / (1024 * 1024)));
    printf("  Throughput:    %.2f MB/s\n", 
           global_stats.bytes_processed / total_time / (1024 * 1024));
    printf("  Errors:        %lu\n", (unsigned long)global_stats.errors);
    printf("  Verify fails:  %lu\n", (unsigned long)global_stats.verification_failures);
    
    int success = (global_stats.errors == 0 && global_stats.verification_failures == 0);
    printf("\n  Result: %s\n\n", success ? "✓ PASSED" : "✗ FAILED");
    
    free(threads);
    free(contexts);
}

/* ============================================== */
/*           RUN RANDOM STRESS TEST              */
/* ============================================== */

static void run_random_stress_test(int num_threads, int duration_sec) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     RANDOM DATA STRESS TEST                                   ║\n");
    printf("║     Threads: %d | Duration: %d seconds                         ║\n",
           num_threads, duration_sec);
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    /* Reset global stats */
    memset(&global_stats, 0, sizeof(global_stats));
    running = 1;
    
    /* Create thread contexts */
    pthread_t *threads = malloc((size_t)num_threads * sizeof(pthread_t));
    RandomThreadContext *contexts = malloc((size_t)num_threads * sizeof(RandomThreadContext));
    
    if (!threads || !contexts) {
        fprintf(stderr, "Failed to allocate thread structures\n");
        free(threads);
        free(contexts);
        return;
    }
    
    /* Initialize contexts */
    for (int i = 0; i < num_threads; i++) {
        contexts[i].thread_id = i;
        memset(&contexts[i].stats, 0, sizeof(ThreadStats));
    }
    
    /* Start threads */
    printf("  Starting %d threads with random-sized operations...\n", num_threads);
    double start_time = get_time_sec();
    
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, random_stress_thread, &contexts[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
        }
    }
    
    /* Progress reporting */
    int elapsed = 0;
    while (elapsed < duration_sec && running) {
        sleep(1);
        elapsed++;
        
        /* Calculate current stats */
        uint64_t total_ops = 0;
        uint64_t total_bytes = 0;
        uint64_t total_errors = 0;
        
        for (int i = 0; i < num_threads; i++) {
            total_ops += contexts[i].stats.operations;
            total_bytes += contexts[i].stats.bytes_processed;
            total_errors += contexts[i].stats.errors + contexts[i].stats.verification_failures;
        }
        
        double throughput = total_bytes / (double)(elapsed) / (1024 * 1024);
        
        printf("  [%3d/%d sec] Ops: %lu | Throughput: %.2f MB/s | Errors: %lu\n",
               elapsed, duration_sec, 
               (unsigned long)total_ops, 
               throughput, 
               (unsigned long)total_errors);
    }
    
    /* Signal threads to stop */
    running = 0;
    
    /* Wait for threads to finish */
    printf("\n  Waiting for threads to finish...\n");
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double total_time = get_time_sec() - start_time;
    
    /* Aggregate statistics */
    ThreadStats random_stats = {0};
    for (int i = 0; i < num_threads; i++) {
        random_stats.operations += contexts[i].stats.operations;
        random_stats.bytes_processed += contexts[i].stats.bytes_processed;
        random_stats.errors += contexts[i].stats.errors;
        random_stats.verification_failures += contexts[i].stats.verification_failures;
    }
    
    /* Print results */
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  RANDOM STRESS TEST RESULTS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    printf("  Duration:      %.2f seconds\n", total_time);
    printf("  Operations:    %lu\n", (unsigned long)random_stats.operations);
    printf("  Bytes:         %lu MB\n", 
           (unsigned long)(random_stats.bytes_processed / (1024 * 1024)));
    printf("  Throughput:    %.2f MB/s\n", 
           random_stats.bytes_processed / total_time / (1024 * 1024));
    printf("  Errors:        %lu\n", (unsigned long)random_stats.errors);
    printf("  Verify fails:  %lu\n", (unsigned long)random_stats.verification_failures);
    
    int success = (random_stats.errors == 0 && random_stats.verification_failures == 0);
    printf("\n  Result: %s\n\n", success ? "✓ PASSED" : "✗ FAILED");
    
    free(threads);
    free(contexts);
}

/* ============================================== */
/*           PRINT USAGE                         */
/* ============================================== */

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n\n", prog);
    printf("Options:\n");
    printf("  --threads=N   Number of threads (default: %d)\n", DEFAULT_NUM_THREADS);
    printf("  --duration=N  Test duration in seconds (default: %d)\n", DEFAULT_DURATION_SEC);
    printf("  --quick       Quick test (10 seconds, 4 threads)\n");
    printf("  --memory      Run memory leak test only\n");
    printf("  --random      Run random data stress test\n");
    printf("  --help        Show this help\n");
}

/* ============================================== */
/*           MAIN                                */
/* ============================================== */

int main(int argc, char **argv) {
    int num_threads = DEFAULT_NUM_THREADS;
    int duration_sec = DEFAULT_DURATION_SEC;
    int quick_mode = 0;
    int memory_only = 0;
    int random_mode = 0;
    
    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--threads=", 10) == 0) {
            num_threads = atoi(argv[i] + 10);
            if (num_threads < 1) num_threads = 1;
            if (num_threads > 64) num_threads = 64;
        } else if (strncmp(argv[i], "--duration=", 11) == 0) {
            duration_sec = atoi(argv[i] + 11);
            if (duration_sec < 1) duration_sec = 1;
        } else if (strcmp(argv[i], "--quick") == 0) {
            quick_mode = 1;
        } else if (strcmp(argv[i], "--memory") == 0) {
            memory_only = 1;
        } else if (strcmp(argv[i], "--random") == 0) {
            random_mode = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }
    
    if (quick_mode) {
        num_threads = 4;
        duration_sec = 10;
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║     KOLIBRI STRESS TEST SUITE                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n\n");
    
    int success = 1;
    
    if (memory_only) {
        /* Memory leak test only */
        success = test_memory_leaks();
    } else if (random_mode) {
        /* Random data stress test */
        run_random_stress_test(num_threads, duration_sec);
        success = (global_stats.errors == 0 && global_stats.verification_failures == 0);
    } else {
        /* Memory leak test */
        printf("  Running memory leak test...\n");
        test_memory_leaks();
        printf("\n");
        
        /* Main stress test */
        run_stress_test(num_threads, duration_sec);
        success = (global_stats.errors == 0 && global_stats.verification_failures == 0);
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  OVERALL RESULT: %s\n", success ? "✓ ALL TESTS PASSED" : "✗ TESTS FAILED");
    printf("═══════════════════════════════════════════════════════════════\n\n");
    
    return success ? 0 : 1;
}
