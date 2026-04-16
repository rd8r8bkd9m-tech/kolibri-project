/*
 * agi_pipeline.h
 *
 * Unified AGI Reasoning Pipeline для Kolibri
 *
 * Архитектура настоящего ИИ:
 *   Stage 1: Intent Classification + World Model Embedding
 *   Stage 2: Multi-Strategy Reasoning (10 типов логики)
 *   Stage 3: Formula Pool + Math Solver
 *   Stage 4: Knowledge Base + Corpus Trainer
 *   Stage 5: Neural Generation (Transformer)
 *   Stage 6: Self-Verification + Confidence Scoring
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_AGI_PIPELINE_H
#define KOLIBRI_AGI_PIPELINE_H

#include "kolibri/corpus_trainer.h"
#include "kolibri/formula.h"
#include "kolibri/intent_classifier.h"
#include "kolibri/math_solver.h"
#include "kolibri/reasoning_engine.h"
#include "kolibri/world_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНФИГУРАЦИЯ PIPELINE
 * ============================================================================ */

/** Стадии pipeline */
typedef enum {
    KAGI_STAGE_INTENT = 0,
    KAGI_STAGE_WORLD_MODEL = 1,
    KAGI_STAGE_REASONING = 2,
    KAGI_STAGE_FORMULA = 3,
    KAGI_STAGE_KNOWLEDGE = 4,
    KAGI_STAGE_NEURAL_GEN = 5,
    KAGI_STAGE_VERIFICATION = 6,
    KAGI_STAGE_COUNT
} KolibriAGIStage;

/** Результат одной стадии */
typedef struct {
    KolibriAGIStage stage;
    char method_name[128];  /* Название метода */
    char answer[4096];      /* Сгенерированный ответ */
    double confidence;      /* Уверенность [0.0, 1.0] */
    int success;            /* Успешно завершена? */
    double duration_ms;     /* Время выполнения */
    char explanation[2048]; /* Объяснение решения */
} KolibriAGIStageResult;

/** Полный результат pipeline */
typedef struct {
    char query[4096];        /* Исходный запрос */
    char final_answer[4096]; /* Финальный ответ */
    double final_confidence; /* Финальная уверенность */

    /* Какая стадия дала ответ */
    KolibriAGIStage winning_stage;
    char winning_method[128];

    /* Результаты всех стадий */
    KolibriAGIStageResult stages[KAGI_STAGE_COUNT];
    int stages_executed;

    /* Метрики */
    double total_duration_ms;
    int reasoning_type_used; /* KolibriReasoningType если использовался reasoning */
    char explanation[4096];  /* Полное объяснение */

    /* Verification */
    int verification_passed;
    double verification_confidence;
} KolibriAGIResult;

/** Конфигурация pipeline */
typedef struct {
    int enable_intent_classification;
    int enable_world_model_embedding;
    int enable_multi_reasoning;
    int enable_formula_pool;
    int enable_knowledge_base;
    int enable_neural_generation;
    int enable_self_verification;

    double confidence_threshold; /* Мин. уверенность для раннего выхода */
    int max_reasoning_time_ms;   /* Макс. время на reasoning */
    int neural_temperature;      /* Температура для генерации (0-100) */

    /* Flags для отладки */
    int verbose;
    int log_each_stage;
} KolibriAGIConfig;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Инициализировать AGI pipeline
 */
int kolibri_agi_init(KolibriAGIConfig *config);

/**
 * Выполнить полный pipeline рассуждения
 *
 * @param query         Входной запрос
 * @param config        Конфигурация
 * @param world_model   World Model для embedding/generation
 * @param formula_pool  Formula pool для вычислений
 * @param corpus        Corpus trainer для semantic lookup
 * @param result        Результат (output)
 * @return 0 при успехе
 */
int kolibri_agi_run(const char *query, const KolibriAGIConfig *config, KwmContext *world_model,
                    KolibriFormulaPool *formula_pool, KlmTrainerContext *corpus, KolibriAGIResult *result);

/**
 * Распечатать результат pipeline
 */
void kolibri_agi_print_result(const KolibriAGIResult *result);

/**
 * Получить название стадии
 */
const char *kolibri_agi_stage_name(KolibriAGIStage stage);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_AGI_PIPELINE_H */
