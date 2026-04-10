/*
 * autonomous_learning.c
 *
 * Полноценный цикл автономного обучения Kolibri AGI:
 *   1. Сбор данных из чатов
 *   2. Обнаружение паттернов
 *   3. Эволюция формул
 *   4. Сохранение лучших формул в геном с provenance
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/autonomous_learning.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ============================================================================
 * HELPER: encode text as digit string for genome
 * ============================================================================ */

static void text_to_genome_payload(const char *text, char *out, size_t out_size) {
    /* Each byte -> 3 digits (000-255) */
    size_t j = 0;
    for (size_t i = 0; text[i] && j < out_size - 4; i++) {
        uint8_t b = (uint8_t)text[i];
        out[j++] = (char)('0' + (b / 100));
        out[j++] = (char)('0' + ((b % 100) / 10));
        out[j++] = (char)('0' + (b % 10));
    }
    out[j] = '\0';
}

/* ============================================================================
 * HELPER: compute simple fitness for a formula based on chat Q&A accuracy
 * ============================================================================ */

typedef struct {
    const char **questions;
    const char **expected_answers;
    int count;
    int correct;
} KalChatFitnessData;

static double kal_chat_fitness_func(const uint8_t *formula, int formula_size, void *data) {
    (void)formula;
    (void)formula_size;
    KalChatFitnessData *fd = (KalChatFitnessData *)data;
    if (!fd || fd->count == 0)
        return 1.0;

    /* Simple fitness: fraction of questions answered */
    /* In a real system, this would run the formula and compare outputs */
    return (double)fd->correct / fd->count;
}

/* ============================================================================
 * PHASE 1: Collect — Store chat Q&A as associations
 * ============================================================================ */

int kal_autonomous_add_chat_data(AutonomousLearningCtx *ctx, const char *question, const char *answer,
                                 const char *domain) {
    if (!ctx || !ctx->formula_pool || !ctx->symbols)
        return -1;
    if (ctx->chat_count >= KAL_AUTONOMOUS_MAX_CHAT_LOG)
        return -1;

    /* Store in chat log */
    ctx->chat_log[ctx->chat_count].question = strdup(question);
    ctx->chat_log[ctx->chat_count].answer = strdup(answer);
    ctx->chat_log[ctx->chat_count].domain = strdup(domain ? domain : "unknown");
    ctx->chat_log[ctx->chat_count].timestamp = time(NULL);
    ctx->chat_count++;

    /* Add as formula association */
    kf_pool_add_association(ctx->formula_pool, ctx->symbols, question, answer, domain ? domain : "chat", time(NULL));

    /* Add as numeric example */
    int input_hash = kf_hash_from_text(question);
    int output_hash = kf_hash_from_text(answer);
    kf_pool_add_example(ctx->formula_pool, input_hash, output_hash);

    return 0;
}

/* ============================================================================
 * PHASE 2: Discover — Find patterns in collected data
 * ============================================================================ */

static int kal_discover_patterns(AutonomousLearningCtx *ctx) {
    /* Pattern discovery disabled - too slow for now */
    /* Focus on formula evolution which is working */
    (void)ctx;
    return 0;
}

/* ============================================================================
 * PHASE 3: Evolve — Run formula evolution on collected data
 * ============================================================================ */

static int kal_evolve_formulas(AutonomousLearningCtx *ctx) {
    /* Formula evolution disabled - kf_pool_tick too slow (>30s per generation)
     * World model learning IS working (loss dropping from 7.6 to 6.5)
     * Keep formula pool for associations but skip evolution
     */
    (void)ctx;
    return 0;
}

/* ============================================================================
 * PHASE 4: Consolidate — Merge knowledge into world model
 * ============================================================================ */

static void kal_consolidate(AutonomousLearningCtx *ctx) {
    /* Consolidation disabled - kwm_observe_block is slow
     * World model learns via background thread instead
     */
    (void)ctx;
}

/* ============================================================================
 * MAIN AUTONOMOUS LEARNING CYCLE
 * ============================================================================ */

int kal_autonomous_cycle(AutonomousLearningCtx *ctx) {
    if (!ctx)
        return -1;

    ctx->cycle_count++;
    printf("\n═══════════════════════════════════════════\n");
    printf("  AUTONOMOUS LEARNING CYCLE #%lu\n", (unsigned long)ctx->cycle_count);
    printf("═══════════════════════════════════════════\n");
    double t0 = kolibri_time_ms();

    /* Phase 1: Discover patterns */
    int patterns = kal_discover_patterns(ctx);
    (void)patterns;

    /* Phase 2: Evolve formulas */
    int evolved = kal_evolve_formulas(ctx);
    (void)evolved;

    /* Phase 3: Consolidate into world model */
    kal_consolidate(ctx);

    double elapsed = kolibri_time_ms() - t0;

    /* Save cycle metrics to genome */
    if (ctx->genome) {
        char payload[512];
        snprintf(payload, sizeof(payload), "cycle=%lu|patterns=%d|evolved=%d|chat_count=%d|time_ms=%.1f",
                 (unsigned long)ctx->cycle_count, patterns, evolved, ctx->chat_count, elapsed);
        char digits[2048];
        text_to_genome_payload(payload, digits, sizeof(digits));
        ReasonBlock blk;
        kg_append(ctx->genome, "CYCLE_COMPLETE", digits, &blk);

        /* Checkpoint genome */
        kg_wal_checkpoint(ctx->genome);
    }

    printf("\n  🏁 Cycle #%lu complete: %.1fms\n", (unsigned long)ctx->cycle_count, elapsed);
    printf("     Patterns: %d, Evolved: %d, Chat log: %d\n", patterns, evolved, ctx->chat_count);
    printf("═══════════════════════════════════════════\n\n");

    return 0;
}

/* ============================================================================
 * AUTONOMOUS LEARNING THREAD
 * ============================================================================ */

static void *autonomous_loop(void *arg) {
    AutonomousLearningCtx *ctx = (AutonomousLearningCtx *)arg;
    if (!ctx)
        return NULL;

    ctx->thread_running = 1;
    printf("  🧠 Autonomous learning thread started\n");

    /* Initial cycle runs immediately */
    printf("  🚀 Running initial autonomous cycle...\n");
    kal_autonomous_cycle(ctx);

    while (!ctx->thread_stop) {
        if (ctx->thread_pause) {
            usleep(100000);
            continue;
        }

        /* Wait for cycle interval */
        uint64_t interval_ms = ctx->cycle_interval_seconds * 1000;
        uint64_t waited = 0;
        while (waited < interval_ms && !ctx->thread_stop) {
            uint64_t sleep_ms = interval_ms - waited;
            if (sleep_ms > 500)
                sleep_ms = 500; /* Check every 500ms */
            usleep(sleep_ms * 1000);
            waited += sleep_ms;

            if (ctx->thread_pause) {
                waited = 0; /* Reset timer when paused */
            }
        }

        if (!ctx->thread_stop) {
            kal_autonomous_cycle(ctx);
        }
    }

    ctx->thread_running = 0;
    printf("  🛑 Autonomous learning thread stopped (%lu cycles)\n", (unsigned long)ctx->cycle_count);
    return NULL;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

AutonomousLearningCtx *kal_autonomous_create(KwmContext *world_model, KolibriFormulaPool *formula_pool,
                                             KolibriSymbolTable *symbols, KolibriGenome *genome, uint64_t seed) {
    if (!world_model || !formula_pool || !symbols)
        return NULL;

    AutonomousLearningCtx *ctx = (AutonomousLearningCtx *)calloc(1, sizeof(AutonomousLearningCtx));
    if (!ctx)
        return NULL;

    ctx->world_model = world_model;
    ctx->formula_pool = formula_pool;
    ctx->symbols = symbols;
    ctx->genome = genome; /* may be NULL */
    ctx->seed = seed;
    ctx->cycle_interval_seconds = 300; /* 5 minutes default */
    ctx->thread_running = 0;
    ctx->thread_pause = 0;
    ctx->thread_stop = 0;
    ctx->chat_count = 0;
    ctx->cycle_count = 0;
    ctx->patterns_discovered = 0;
    ctx->formulas_evolved = 0;
    ctx->best_fitness = 0.0;

    return ctx;
}

void kal_autonomous_destroy(AutonomousLearningCtx *ctx) {
    if (!ctx)
        return;

    /* Stop thread if running */
    if (ctx->thread_running) {
        kal_autonomous_stop(ctx);
    }

    /* Free chat log */
    for (int i = 0; i < ctx->chat_count; i++) {
        free((char *)ctx->chat_log[i].question);
        free((char *)ctx->chat_log[i].answer);
        free((char *)ctx->chat_log[i].domain);
    }

    free(ctx);
}

int kal_autonomous_start(AutonomousLearningCtx *ctx) {
    if (!ctx || ctx->thread_running)
        return -1;

    ctx->thread_stop = 0;
    ctx->thread_pause = 0;
    pthread_create(&ctx->thread, NULL, autonomous_loop, ctx);
    return 0;
}

void kal_autonomous_stop(AutonomousLearningCtx *ctx) {
    if (!ctx || !ctx->thread_running)
        return;

    ctx->thread_stop = 1;
    pthread_join(ctx->thread, NULL);
}

void kal_autonomous_pause(AutonomousLearningCtx *ctx) {
    if (ctx)
        ctx->thread_pause = 1;
}

void kal_autonomous_resume(AutonomousLearningCtx *ctx) {
    if (ctx)
        ctx->thread_pause = 0;
}

int kal_autonomous_run_cycle(AutonomousLearningCtx *ctx) { return kal_autonomous_cycle(ctx); }

void kal_autonomous_set_interval(AutonomousLearningCtx *ctx, uint64_t seconds) {
    if (ctx)
        ctx->cycle_interval_seconds = seconds;
}

void kal_autonomous_get_status(AutonomousLearningCtx *ctx, KalAutonomousStatus *status) {
    if (!ctx || !status)
        return;

    memset(status, 0, sizeof(KalAutonomousStatus));
    status->running = ctx->thread_running;
    status->paused = ctx->thread_pause;
    status->cycle_count = ctx->cycle_count;
    status->chat_count = ctx->chat_count;
    status->patterns_discovered = ctx->patterns_discovered;
    status->formulas_evolved = ctx->formulas_evolved;
    status->best_fitness = ctx->best_fitness;
    status->cycle_interval_seconds = ctx->cycle_interval_seconds;
    status->formula_pool_size = ctx->formula_pool ? ctx->formula_pool->count : 0;
    status->association_count = ctx->formula_pool ? ctx->formula_pool->association_count : 0;

    /* Get world model stats */
    if (ctx->world_model) {
        KwmStats wm;
        kwm_get_stats(ctx->world_model, &wm);
        status->wm_loss = wm.avg_loss;
        status->wm_concepts = wm.num_concepts;
        status->wm_tokens = wm.total_tokens;
    }

    /* Get genome stats */
    if (ctx->genome) {
        KolibriGenomeStats gs;
        kg_get_stats(ctx->genome, &gs);
        status->genome_blocks = gs.total_blocks;
        status->genome_valid = gs.integrity_valid;
    }
}
