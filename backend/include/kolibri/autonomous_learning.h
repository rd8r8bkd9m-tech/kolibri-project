/*
 * autonomous_learning.h
 *
 * Автономное обучение Kolibri AGI:
 *   Сбор → Паттерны → Эволюция → Сохранение в геном
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_AUTONOMOUS_LEARNING_H
#define KOLIBRI_AUTONOMOUS_LEARNING_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#include "kolibri/world_model.h"
#include "kolibri/formula.h"
#include "kolibri/genome.h"
#include "kolibri/symbol_table.h"
#include "kolibri/pattern_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KAL_AUTONOMOUS_MAX_CHAT_LOG 1000

/* Chat entry log */
typedef struct {
    const char *question;
    const char *answer;
    const char *domain;
    const char *method;
    double confidence;
    uint64_t timestamp;
} KalChatEntry;

/* Status snapshot */
typedef struct {
    int running;
    int paused;
    uint64_t cycle_count;
    int chat_count;
    int patterns_discovered;
    int formulas_evolved;
    double best_fitness;
    uint64_t cycle_interval_seconds;
    size_t formula_pool_size;
    size_t association_count;
    double wm_loss;
    size_t wm_concepts;
    uint64_t wm_tokens;
    uint64_t genome_blocks;
    int genome_valid;
} KalAutonomousStatus;

/* Main autonomous learning context */
typedef struct {
    /* Module references */
    KwmContext *world_model;
    KolibriFormulaPool *formula_pool;
    KolibriSymbolTable *symbols;
    KolibriGenome *genome;
    
    /* Chat data log */
    KalChatEntry chat_log[KAL_AUTONOMOUS_MAX_CHAT_LOG];
    int chat_count;
    
    /* Cycle counters */
    uint64_t cycle_count;
    int patterns_discovered;
    int formulas_evolved;
    double best_fitness;
    
    /* Thread control */
    pthread_t thread;
    int thread_running;
    int thread_pause;
    int thread_stop;
    uint64_t cycle_interval_seconds;
    uint64_t seed;
} AutonomousLearningCtx;

/* ============================================================================
 * LIFECYCLE
 * ============================================================================ */

/** Create autonomous learning context */
AutonomousLearningCtx* kal_autonomous_create(
    KwmContext *world_model,
    KolibriFormulaPool *formula_pool,
    KolibriSymbolTable *symbols,
    KolibriGenome *genome,
    uint64_t seed
);

/** Destroy context and free resources */
void kal_autonomous_destroy(AutonomousLearningCtx *ctx);

/* ============================================================================
 * THREAD CONTROL
 * ============================================================================ */

/** Start autonomous learning background thread */
int kal_autonomous_start(AutonomousLearningCtx *ctx);

/** Stop the background thread */
void kal_autonomous_stop(AutonomousLearningCtx *ctx);

/** Pause the background thread */
void kal_autonomous_pause(AutonomousLearningCtx *ctx);

/** Resume the background thread */
void kal_autonomous_resume(AutonomousLearningCtx *ctx);

/** Run one cycle synchronously */
int kal_autonomous_run_cycle(AutonomousLearningCtx *ctx);

/** Set cycle interval in seconds (default: 300 = 5 min) */
void kal_autonomous_set_interval(AutonomousLearningCtx *ctx, uint64_t seconds);

/* ============================================================================
 * DATA COLLECTION
 * ============================================================================ */

/** Add chat Q&A to the learning pipeline */
int kal_autonomous_add_chat_data(AutonomousLearningCtx *ctx,
                                  const char *question, const char *answer,
                                  const char *domain);

/* ============================================================================
 * STATUS
 * ============================================================================ */

/** Get current status snapshot */
void kal_autonomous_get_status(AutonomousLearningCtx *ctx,
                                KalAutonomousStatus *status);

/* ============================================================================
 * TIME HELPER (same as in kolibri_http_server)
 * ============================================================================ */

static inline double kolibri_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AUTONOMOUS_LEARNING_H */
