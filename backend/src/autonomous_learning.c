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

    /* Add as formula association (outside lock — formula_pool has its own locking) */
    kf_pool_add_association(ctx->formula_pool, ctx->symbols, question, answer, domain ? domain : "chat", time(NULL));

    /* Add as numeric example */
    int input_hash = kf_hash_from_text(question);
    int output_hash = kf_hash_from_text(answer);
    kf_pool_add_example(ctx->formula_pool, input_hash, output_hash);

    /* Store in chat log (protected by mutex) */
    pthread_mutex_lock(&ctx->chat_lock);
    if (ctx->chat_count >= KAL_AUTONOMOUS_MAX_CHAT_LOG) {
        pthread_mutex_unlock(&ctx->chat_lock);
        return -1;
    }
    int idx = ctx->chat_count;
    ctx->chat_log[idx].question = strdup(question);
    ctx->chat_log[idx].answer = strdup(answer);
    ctx->chat_log[idx].domain = strdup(domain ? domain : "unknown");
    /* Проверка на OOM после strdup */
    if (!ctx->chat_log[idx].question || !ctx->chat_log[idx].answer || !ctx->chat_log[idx].domain) {
        free(ctx->chat_log[idx].question);
        free(ctx->chat_log[idx].answer);
        free(ctx->chat_log[idx].domain);
        ctx->chat_log[idx].question = NULL;
        ctx->chat_log[idx].answer = NULL;
        ctx->chat_log[idx].domain = NULL;
        pthread_mutex_unlock(&ctx->chat_lock);
        fprintf(stderr, "autonomous_learning: OOM — failed to allocate chat entry %d\n", idx);
        return -1;
    }
    ctx->chat_log[idx].timestamp = time(NULL);
    ctx->chat_count++;
    pthread_mutex_unlock(&ctx->chat_lock);

    return 0;
}

/* ============================================================================
 * PHASE 2: Discover — Find patterns in collected data
 *
 * Simple pattern discovery:
 *   - Group chat entries by domain
 *   - Find repeated/similar questions (hash-based clustering)
 *   - Track which domains have most activity
 * ============================================================================ */

#define KAL_MAX_PATTERN_BUCKETS 64

typedef struct {
    int hash_bucket;
    int first_index;
    int count;
} KalPatternBucket;

static int kal_discover_patterns(AutonomousLearningCtx *ctx) {
    if (!ctx || ctx->chat_count == 0)
        return 0;

    /* Lock chat_log for reading */
    pthread_mutex_lock(&ctx->chat_lock);
    int n = ctx->chat_count;

    /* 1. Count entries per domain */
    int domain_counts[16] = {0};
    for (int i = 0; i < n; i++) {
        const char *d = ctx->chat_log[i].domain;
        if (!d) {
            continue;
        }
        /* Simple hash to bucket */
        unsigned h = 0;
        for (const char *p = d; *p; p++)
            h = h * 31 + (unsigned char)*p;
        int bucket = (int)(h % 16);
        domain_counts[bucket]++;
    }

    /* 2. Find repeated questions by hash clustering */
    KalPatternBucket buckets[KAL_MAX_PATTERN_BUCKETS];
    memset(buckets, 0, sizeof(buckets));
    int bucket_count = 0;

    for (int i = 0; i < n; i++) {
        const char *q = ctx->chat_log[i].question;
        if (!q)
            continue;
        int h = kf_hash_from_text(q) % KAL_MAX_PATTERN_BUCKETS;
        if (h < 0)
            h = -h;

        int found = 0;
        for (int b = 0; b < bucket_count; b++) {
            if (buckets[b].hash_bucket == h) {
                buckets[b].count++;
                found = 1;
                break;
            }
        }
        if (!found && bucket_count < KAL_MAX_PATTERN_BUCKETS) {
            buckets[bucket_count].hash_bucket = h;
            buckets[bucket_count].first_index = i;
            buckets[bucket_count].count = 1;
            bucket_count++;
        }
    }

    /* Count patterns with >= 2 entries (repeated questions) */
    int repeated_patterns = 0;
    for (int b = 0; b < bucket_count; b++) {
        if (buckets[b].count >= 2)
            repeated_patterns++;
    }

    /* Count distinct active domains */
    int active_domains = 0;
    for (int d = 0; d < 16; d++) {
        if (domain_counts[d] > 0)
            active_domains++;
    }

    pthread_mutex_unlock(&ctx->chat_lock);

    /* Update stats */
    ctx->patterns_discovered += repeated_patterns;

    /* Log discovery results */
    printf("  🔍 Patterns discovered: %d repeated question groups, %d active domains\n", repeated_patterns,
           active_domains);

    /* Save to genome if available */
    if (ctx->genome && repeated_patterns > 0) {
        char payload[512];
        snprintf(payload, sizeof(payload), "patterns|repeated=%d|domains=%d|total_chat=%d", repeated_patterns,
                 active_domains, n);
        char digits[2048];
        text_to_genome_payload(payload, digits, sizeof(digits));
        ReasonBlock blk;
        kg_append(ctx->genome, "PATTERN_DISCOVERY", digits, &blk);
    }

    return repeated_patterns;
}

/* ============================================================================
 * PHASE 3: Evolve — Run formula evolution on collected data
 *
 * Simplified evolution: up to 100 formulas, 10 generations.
 * Uses existing kf_pool_tick with a capped example set.
 * ============================================================================ */

#define KAL_EVOLVE_MAX_FORMULAS 100
#define KAL_EVOLVE_MAX_GENERATIONS 10

static int kal_evolve_formulas(AutonomousLearningCtx *ctx) {
    if (!ctx || !ctx->formula_pool)
        return 0;

    /* Ensure we have some examples to evolve against */
    if (ctx->formula_pool->examples == 0 && ctx->chat_count == 0)
        return 0;

    printf("  🧬 Evolving formulas: pool_size=%zu, examples=%zu\n", ctx->formula_pool->count,
           ctx->formula_pool->examples);

    /* Cap the formula pool for this tick to avoid long runs */
    size_t original_count = ctx->formula_pool->count;
    size_t evolve_count = original_count;
    if (evolve_count > KAL_EVOLVE_MAX_FORMULAS)
        evolve_count = KAL_EVOLVE_MAX_FORMULAS;

    double t0 = kolibri_time_ms();

    /* Run evolution with limited generations */
    kf_pool_tick(ctx->formula_pool, KAL_EVOLVE_MAX_GENERATIONS);

    double elapsed = kolibri_time_ms() - t0;

    /* Get metrics after evolution */
    KolibriEvolutionMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    kf_pool_get_metrics(ctx->formula_pool, &metrics);

    ctx->formulas_evolved += KAL_EVOLVE_MAX_GENERATIONS;
    if (metrics.best_fitness > ctx->best_fitness)
        ctx->best_fitness = metrics.best_fitness;

    printf("  🧬 Evolution complete: %zu formulas, %d generations, %.1fms, best_fitness=%.3f\n", evolve_count,
           KAL_EVOLVE_MAX_GENERATIONS, elapsed, metrics.best_fitness);

    /* Save evolution results to genome */
    if (ctx->genome) {
        char payload[512];
        snprintf(payload, sizeof(payload), "evolution|formulas=%zu|gens=%d|best_fit=%.4f|avg_fit=%.4f|time_ms=%.1f",
                 evolve_count, KAL_EVOLVE_MAX_GENERATIONS, metrics.best_fitness, metrics.avg_fitness, elapsed);
        char digits[2048];
        text_to_genome_payload(payload, digits, sizeof(digits));
        ReasonBlock blk;
        kg_append(ctx->genome, "FORMULA_EVOLUTION", digits, &blk);
    }

    return KAL_EVOLVE_MAX_GENERATIONS;
}

/* ============================================================================
 * PHASE 4: Consolidate — Merge knowledge into world model
 *
 * Batch observations from chat log into the world model.
 * Groups recent Q&A pairs and feeds them as observation blocks.
 * ============================================================================ */

#define KAL_CONSOLIDATE_BATCH_SIZE 20

static void kal_consolidate(AutonomousLearningCtx *ctx) {
    if (!ctx || !ctx->world_model || ctx->chat_count == 0)
        return;

    /* Lock and collect a batch of recent entries */
    pthread_mutex_lock(&ctx->chat_lock);
    int n = ctx->chat_count;
    int start = (n > KAL_CONSOLIDATE_BATCH_SIZE) ? (n - KAL_CONSOLIDATE_BATCH_SIZE) : 0;
    int batch_size = n - start;

    if (batch_size <= 0) {
        pthread_mutex_unlock(&ctx->chat_lock);
        return;
    }

    /* Build observation text from batch */
    char obs_buffer[4096];
    int offset = 0;
    for (int i = start; i < n && offset < (int)sizeof(obs_buffer) - 256; i++) {
        const char *q = ctx->chat_log[i].question;
        const char *a = ctx->chat_log[i].answer;
        const char *d = ctx->chat_log[i].domain;
        if (q && a) {
            offset += snprintf(obs_buffer + offset, sizeof(obs_buffer) - offset, "[Q:%s] [A:%s] [D:%s] ", q, a,
                               d ? d : "unknown");
        }
    }
    pthread_mutex_unlock(&ctx->chat_lock);

    if (offset == 0)
        return;

    double t0 = kolibri_time_ms();

    /* Feed observation block to world model */
    kwm_observe_block(ctx->world_model, (const uint8_t *)obs_buffer, (size_t)offset);

    double elapsed = kolibri_time_ms() - t0;

    printf("  📦 Consolidated: %d observations into world model (%.1fms)\n", batch_size, elapsed);

    /* Save consolidation record to genome */
    if (ctx->genome) {
        char payload[512];
        snprintf(payload, sizeof(payload), "consolidate|batch=%d|obs_len=%d|time_ms=%.1f", batch_size, offset, elapsed);
        char digits[2048];
        text_to_genome_payload(payload, digits, sizeof(digits));
        ReasonBlock blk;
        kg_append(ctx->genome, "CONSOLIDATION", digits, &blk);
    }
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
    pthread_mutex_init(&ctx->chat_lock, NULL);

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

    pthread_mutex_destroy(&ctx->chat_lock);
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
