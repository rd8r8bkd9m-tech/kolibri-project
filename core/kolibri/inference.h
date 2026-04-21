/*
 * inference.h
 *
 * ЭКСТРЕМАЛЬНЫЕ ЛИМИТЫ ДЛЯ БЕНЧМАРКОВ (1 МБ+)
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_INFERENCE_H
#define KOLIBRI_INFERENCE_H

#include <stddef.h>
#include <stdint.h>
#include "kolibri/decimal.h"
#include "kolibri/logical_memory.h"
#include "kolibri/formula_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== КОНФИГУРАЦИЯ (Extreme Capacity) ========== */

#define KOLIBRI_INF_MAX_QUERY      1048576 /* 1 МБ входа */
#define KOLIBRI_INF_MAX_RESPONSE   1048576 /* 1 МБ ответа */
#define KOLIBRI_INF_MAX_DIGITS     4194304 /* 4 млн цифр (~1.3 МБ текста) */
#define KOLIBRI_INF_MAX_GENES      4096
#define KOLIBRI_INF_MAX_CONTEXT    16
#define KOLIBRI_INF_MAX_STEPS      64
#define KOLIBRI_INF_DIGIT_VOTERS   10

/* ========== ТИПЫ ========== */

typedef enum {
    KOLIBRI_INF_DIRECT,
    KOLIBRI_INF_FORMULA,
    KOLIBRI_INF_LOGICAL,
    KOLIBRI_INF_CHAIN,
    KOLIBRI_INF_HYBRID,
    KOLIBRI_INF_DIRECT_COGNITION
} KolibriInferenceStrategy;

typedef struct {
    char *response; /* Сделаем указателем для экстремальных объемов */
    uint8_t *digit_stream;
    size_t digit_count;
    uint8_t genes[KOLIBRI_INF_MAX_GENES][32];
    size_t gene_count;
    double confidence;
    double duration_ms;
} KolibriCognitionResult;

typedef struct {
    char description[256];
    char result[512];
    double confidence;
    double duration_ms;
} KolibriInferenceStep;

typedef struct {
    double channels[KOLIBRI_INF_DIGIT_VOTERS];
    uint8_t winner_digit;
    double winner_score;
    double runner_up_score;
    double consensus;
} KolibriNumericVoteSummary;

typedef struct {
    char query_kind[32];
    char canonical_topic[128];
    char definition_entity[128];
    size_t topic_token_count;
} KolibriQuerySemanticSummary;

typedef struct {
    char response[KOLIBRI_INF_MAX_RESPONSE];
    size_t response_length;
    KolibriInferenceStep steps[KOLIBRI_INF_MAX_STEPS];
    size_t step_count;
    double total_confidence;
    double total_duration_ms;
    size_t knowledge_hits;
    size_t formulas_applied;
    size_t logic_rules_fired;
    KolibriNumericVoteSummary numeric_vote;
    uint8_t digit_winner;
    KolibriQuerySemanticSummary query_semantics;
    char sources[KOLIBRI_INF_MAX_CONTEXT][256];
    size_t source_count;
} KolibriInferenceResult;

typedef struct {
    KolibriInferenceStrategy strategy;
    LogicalMemory *memory;
    MetaFormulaStore *formula_store;
    double temperature;
    size_t max_steps;
    size_t knowledge_limit;
    uint64_t total_queries;
    uint64_t total_tokens_in;
    uint64_t total_tokens_out;
    double avg_confidence;
    double avg_duration_ms;
} KolibriInferenceContext;

/* ========== API ========== */

KolibriInferenceContext* kolibri_inference_create(void);
void kolibri_inference_destroy(KolibriInferenceContext *ctx);

int kolibri_inference_set_strategy(
    KolibriInferenceContext *ctx,
    KolibriInferenceStrategy strategy
);

int kolibri_inference_set_temperature(
    KolibriInferenceContext *ctx,
    double temperature
);

int kolibri_inference_run(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceResult *result
);

int kolibri_inference_step(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceStep *step
);

int kolibri_inference_get_stats(
    const KolibriInferenceContext *ctx,
    uint64_t *total_queries,
    double *avg_confidence,
    double *avg_duration
);

void kolibri_inference_reset_stats(KolibriInferenceContext *ctx);

int kolibri_inference_think(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriCognitionResult *result
);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_INFERENCE_H */
