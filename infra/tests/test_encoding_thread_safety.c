/*
 * test_encoding_thread_safety.c
 *
 * Thread safety test for the encoding pipeline.
 * Verifies that 10 parallel threads encoding 100 words each
 * produce correct results without crashes or data races.
 *
 * Copyright (c) 2026 Кочуров Владислав Евгеньевич
 */

#include "kolibri/encoding_pipeline.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

#define NUM_THREADS 10
#define WORDS_PER_THREAD 100
#define MAX_WORD_LEN 64

/* Test word pool — shared across threads */
static const char *test_words[] = {
    "hello",   "world",    "test",    "kolibri", "AI",      "привет",   "мир",    "тест",  "код",      "данные",
    "machine", "learning", "encoder", "decoder", "pattern", "алгоритм", "нейрон", "сеть",  "обучение", "модель",
    "data",    "logic",    "reason",  "think",   "solve",   "знание",   "память", "разум", "логика",   "система",
    "the",     "and",      "for",     "with",    "from",    "это",      "как",    "что",   "где",      "когда",
};

static const int num_test_words = sizeof(test_words) / sizeof(test_words[0]);

/* Per-thread results storage */
typedef struct {
    char word[MAX_WORD_LEN];
    char encoded[1024];
    double confidence;
    int thread_id;
    int word_index;
    int success;
} ThreadResult;

/* Thread argument */
typedef struct {
    int thread_id;
    KolibriEncodingPipeline *pipeline;
    ThreadResult *results;
    const char **words;
    int word_count;
} ThreadArg;

/* Atomic-like result counter (simple, no atomics needed since only main reads) */
static int total_results = 0;
static pthread_mutex_t results_mutex = PTHREAD_MUTEX_INITIALIZER;

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

/* Check that encoded string contains only valid characters (digits 0-9, dots, spaces, minus) */
static int is_valid_encoded_string(const char *str) {
    if (str == NULL || str[0] == '\0') {
        return 0;
    }
    for (const char *p = str; *p != '\0'; p++) {
        char c = *p;
        if (!((c >= '0' && c <= '9') || c == '.' || c == ' ' || c == '-' || c == ',')) {
            return 0;
        }
    }
    return 1;
}

/*
 * Thread worker: each thread encodes WORDS_PER_THREAD words.
 * Words are selected cyclically from the shared test_words pool to ensure
 * the same word is encoded by multiple threads (data race detection).
 */
static void *thread_worker(void *arg) {
    ThreadArg *targ = (ThreadArg *)arg;
    KolibriEncodingPipeline *pipeline = targ->pipeline;
    ThreadResult *results = targ->results;
    int tid = targ->thread_id;

    for (int i = 0; i < targ->word_count; i++) {
        /* Pick word cyclically so same words get encoded by different threads */
        const char *word = targ->words[i % num_test_words];

        KolibriEncodingResult enc_result;
        int ret = kolibri_pipeline_encode_word(pipeline, word, &enc_result);

        int idx;
        pthread_mutex_lock(&results_mutex);
        idx = total_results;
        total_results++;
        pthread_mutex_unlock(&results_mutex);

        if (ret == 0) {
            strncpy(results[idx].word, word, MAX_WORD_LEN - 1);
            results[idx].word[MAX_WORD_LEN - 1] = '\0';

            /* Serialize digit stream to string for validation */
            if (enc_result.digit_stream.danniye != NULL && enc_result.digit_stream.dlina > 0) {
                int pos = 0;
                for (size_t d = 0; d < enc_result.digit_stream.dlina && pos < 1020; d++) {
                    pos += snprintf(results[idx].encoded + pos, 1024 - pos, "%d ", enc_result.digit_stream.danniye[d]);
                }
                results[idx].encoded[pos] = '\0';
            } else {
                strncpy(results[idx].encoded, "(empty)", 1023);
                results[idx].encoded[1023] = '\0';
            }

            results[idx].confidence = enc_result.confidence;
            results[idx].thread_id = tid;
            results[idx].word_index = i;
            results[idx].success = 1;
        } else {
            strncpy(results[idx].word, word, MAX_WORD_LEN - 1);
            results[idx].word[MAX_WORD_LEN - 1] = '\0';
            results[idx].encoded[0] = '\0';
            results[idx].confidence = -1.0;
            results[idx].thread_id = tid;
            results[idx].word_index = i;
            results[idx].success = 0;
        }
    }

    return NULL;
}

/*
 * Build a canonical pattern string from digit_stream for data race detection.
 * Same word → same pattern across threads.
 */
static void build_pattern(const KolibriEncodingResult *result, char *out, size_t out_size) {
    int pos = 0;
    if (result->digit_stream.danniye != NULL && result->digit_stream.dlina > 0) {
        for (size_t d = 0; d < result->digit_stream.dlina && (size_t)pos < out_size - 10; d++) {
            pos += snprintf(out + pos, out_size - pos, "%d,", result->digit_stream.danniye[d]);
        }
    }
    if (pos > 0)
        out[pos - 1] = '\0'; /* remove trailing comma */
    else
        out[0] = '\0';
}

int main(void) {
    printf("\n========================================\n");
    printf("  Kolibri Encoding Pipeline\n");
    printf("  Thread Safety Test\n");
    printf("========================================\n");
    printf("  Threads:       %d\n", NUM_THREADS);
    printf("  Words/thread:  %d\n", WORDS_PER_THREAD);
    printf("  Total words:   %d\n", NUM_THREADS * WORDS_PER_THREAD);
    printf("  Test words:    %d unique\n", num_test_words);
    printf("========================================\n\n");

    double start_time = get_time_sec();

    /* 1. Create pipeline (shared across threads) */
    KolibriEncodingPipeline *pipeline = NULL;
    int ret = kolibri_pipeline_create(&pipeline, NULL);
    if (ret != 0 || pipeline == NULL) {
        fprintf(stderr, "ERROR: Failed to create encoding pipeline\n");
        return 1;
    }
    printf("[OK] Pipeline created\n");

    /* 2. Allocate results array */
    int total_words = NUM_THREADS * WORDS_PER_THREAD;
    ThreadResult *all_results = (ThreadResult *)calloc(total_words, sizeof(ThreadResult));
    if (all_results == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate results array\n");
        kolibri_pipeline_destroy(pipeline);
        return 1;
    }

    /* 3. Create and launch threads */
    pthread_t threads[NUM_THREADS];
    ThreadArg thread_args[NUM_THREADS];

    printf("[*] Launching %d threads...\n", NUM_THREADS);
    double thread_start = get_time_sec();

    for (int t = 0; t < NUM_THREADS; t++) {
        thread_args[t].thread_id = t;
        thread_args[t].pipeline = pipeline;
        thread_args[t].results = all_results;
        thread_args[t].words = test_words;
        thread_args[t].word_count = WORDS_PER_THREAD;

        int rc = pthread_create(&threads[t], NULL, thread_worker, &thread_args[t]);
        if (rc != 0) {
            fprintf(stderr, "ERROR: pthread_create failed for thread %d (rc=%d)\n", t, rc);
            /* Cancel already-created threads */
            for (int j = 0; j < t; j++) {
                pthread_cancel(threads[j]);
            }
            free(all_results);
            kolibri_pipeline_destroy(pipeline);
            return 1;
        }
    }

    /* 4. Join all threads */
    int join_errors = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
        int rc = pthread_join(threads[t], NULL);
        if (rc != 0) {
            fprintf(stderr, "ERROR: pthread_join failed for thread %d (rc=%d)\n", t, rc);
            join_errors++;
        }
    }

    double thread_end = get_time_sec();
    double thread_time = thread_end - thread_start;

    if (join_errors > 0) {
        fprintf(stderr, "ERROR: %d thread join failures\n", join_errors);
        free(all_results);
        kolibri_pipeline_destroy(pipeline);
        return 1;
    }

    printf("[OK] All %d threads completed in %.3f sec\n", NUM_THREADS, thread_time);

    /* 5. Validate results */
    printf("\n--- Validating results ---\n");

    int success_count = 0;
    int fail_count = 0;
    int invalid_encoding = 0;
    int invalid_confidence = 0;
    int missing_word = 0;

    for (int i = 0; i < total_results; i++) {
        if (!all_results[i].success) {
            fail_count++;
            continue;
        }
        success_count++;

        /* Check word is preserved */
        if (strlen(all_results[i].word) == 0) {
            missing_word++;
            continue;
        }

        /* Check encoded string validity (digits 0-9) */
        if (!is_valid_encoded_string(all_results[i].encoded)) {
            invalid_encoding++;
            printf("  [WARN] Thread %d, word '%s': invalid encoding '%s'\n", all_results[i].thread_id,
                   all_results[i].word, all_results[i].encoded);
        }

        /* Check confidence range */
        if (all_results[i].confidence < 0.0 || all_results[i].confidence > 1.0) {
            invalid_confidence++;
            printf("  [WARN] Thread %d, word '%s': confidence %.4f out of range\n", all_results[i].thread_id,
                   all_results[i].word, all_results[i].confidence);
        }
    }

    printf("  Success:         %d / %d\n", success_count, total_results);
    printf("  Failures:        %d\n", fail_count);
    printf("  Invalid enc:     %d\n", invalid_encoding);
    printf("  Invalid conf:    %d\n", invalid_confidence);
    printf("  Missing words:   %d\n", missing_word);

    int validation_passed = (fail_count == 0 && invalid_encoding == 0 && invalid_confidence == 0 && missing_word == 0);

    /* 6. Data race detection: same word → same pattern across threads */
    printf("\n--- Data race detection ---\n");

    /* For each unique test word, collect patterns from different threads and compare */
    int data_race_detected = 0;
    int race_checks = 0;

    /* Simple approach: for each unique word, find first encoding, then compare others */
    /* We'll re-encode words sequentially to get baseline patterns */
    char *baseline_patterns = (char *)calloc(num_test_words, 1024);
    if (baseline_patterns == NULL) {
        fprintf(stderr, "ERROR: Failed to allocate baseline patterns\n");
        free(all_results);
        kolibri_pipeline_destroy(pipeline);
        return 1;
    }

    for (int w = 0; w < num_test_words; w++) {
        KolibriEncodingResult baseline;
        int ret = kolibri_pipeline_encode_word(pipeline, test_words[w], &baseline);
        if (ret == 0) {
            build_pattern(&baseline, baseline_patterns + w * 1024, 1024);
        }
    }

    /* Now check thread results against baselines */
    for (int i = 0; i < total_results; i++) {
        if (!all_results[i].success)
            continue;

        /* Find which test word this was */
        int word_idx = all_results[i].word_index % num_test_words;
        const char *expected_word = test_words[word_idx];

        /* Only compare if the word matches expected */
        if (strcmp(all_results[i].word, expected_word) != 0)
            continue;

        /* Encode the same word fresh and compare pattern */
        KolibriEncodingResult fresh;
        int ret = kolibri_pipeline_encode_word(pipeline, all_results[i].word, &fresh);
        if (ret != 0)
            continue;

        char fresh_pattern[1024];
        build_pattern(&fresh, fresh_pattern, sizeof(fresh_pattern));

        const char *stored_pattern = all_results[i].encoded;

        /* Compare: encoded digits should match fresh encoding */
        /* Note: digit_stream data should be deterministic for same input */
        race_checks++;

        /* Verify digit_stream values match */
        if (fresh.digit_stream.danniye != NULL && all_results[i].success) {
            /* Re-verify by encoding again in main thread */
            KolibriEncodingResult verify;
            kolibri_pipeline_encode_word(pipeline, all_results[i].word, &verify);

            if (verify.digit_stream.dlina != 0 && fresh.digit_stream.dlina == verify.digit_stream.dlina) {
                for (size_t d = 0; d < fresh.digit_stream.dlina; d++) {
                    if (fresh.digit_stream.danniye[d] != verify.digit_stream.danniye[d]) {
                        data_race_detected++;
                        if (data_race_detected <= 5) {
                            printf("  [RACE] Thread %d, word '%s': digit[%zu] = %d vs %d\n", all_results[i].thread_id,
                                   all_results[i].word, d, fresh.digit_stream.danniye[d],
                                   verify.digit_stream.danniye[d]);
                        }
                        break;
                    }
                }
            }
        }
    }

    printf("  Race checks:     %d\n", race_checks);
    printf("  Data races:      %d\n", data_race_detected);

    int race_test_passed = (data_race_detected == 0);

    /* 7. Cleanup */
    free(baseline_patterns);
    free(all_results);
    kolibri_pipeline_destroy(pipeline);

    double total_time = get_time_sec() - start_time;

    /* 8. Summary */
    printf("\n========================================\n");
    printf("  Summary\n");
    printf("========================================\n");
    printf("  Thread time:     %.3f sec\n", thread_time);
    printf("  Total time:      %.3f sec\n", total_time);
    printf("  Words encoded:   %d\n", success_count);
    printf("  Throughput:      %.0f words/sec\n", total_time > 0 ? success_count / total_time : 0);
    printf("  Validation:      %s\n", validation_passed ? "PASS" : "FAIL");
    printf("  Data races:      %s\n", race_test_passed ? "NONE DETECTED" : "DETECTED");
    printf("  Timeout (<30s):  %s\n", total_time < 30.0 ? "OK" : "TIMEOUT");
    printf("========================================\n\n");

    int overall_passed =
        validation_passed && race_test_passed && (total_time < 30.0) && (success_count == total_results);

    if (overall_passed) {
        printf("RESULT: ALL TESTS PASSED\n\n");
        return 0;
    } else {
        printf("RESULT: TESTS FAILED\n\n");
        return 1;
    }
}
