/*
 * action_engine.h
 *
 * Движок действий (Action Loops) для Kolibri AGI.
 * Реализует концепцию "Reasoning-guided action": 
 * ИИ не просто вызывает инструменты, а сначала обосновывает необходимость вызова,
 * планирует последовательность и верифицирует результат.
 */

#ifndef KOLIBRI_ACTION_ENGINE_H
#define KOLIBRI_ACTION_ENGINE_H

#include "reasoning_engine.h"
#include "genome.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

typedef enum {
    KAE_ACTION_TOOL_USE = 0,    /* Использование инструмента (shell, python, API) */
    KAE_ACTION_OBSERVATION = 1, /* Наблюдение за состоянием системы */
    KAE_ACTION_VERIFICATION = 2,/* Верификация результата предыдущего действия */
    KAE_ACTION_REASONING = 3    /* Внутреннее рассуждение/планирование */
} KolibriActionType;

typedef enum {
    KAE_STATUS_PENDING,
    KAE_STATUS_EXECUTING,
    KAE_STATUS_SUCCESS,
    KAE_STATUS_FAILURE,
    KAE_STATUS_BLOCKED
} KolibriActionStatus;

typedef struct {
    char name[64];
    char description[256];
    char tool_id[64];
    char parameters[1024];
    KolibriActionType type;
    KolibriActionStatus status;
    char reasoning_justification[512]; /* Почему это действие необходимо? */
    double confidence;
    char result[2048];
} KolibriAction;

typedef struct {
    char goal[512];
    KolibriAction actions[16];
    int num_actions;
    int current_action_idx;
    KolibriActionStatus overall_status;
    double progress;
    KolibriGenome *genome; /* For provenance logging */
} KolibriActionLoop;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Инициализировать цикл действий для достижения цели
 */
int kolibri_ae_init_loop(KolibriActionLoop *loop, const char *goal, KolibriGenome *genome);

/**
 * Спланировать следующий шаг на основе текущего состояния и reasoning
 */
int kolibri_ae_plan_step(KolibriActionLoop *loop, const KolibriReasoningResult *reasoning);

/**
 * Выполнить текущее действие
 */
int kolibri_ae_execute_current(KolibriActionLoop *loop);

/**
 * Верифицировать результат последнего действия
 */
int kolibri_ae_verify_last(KolibriActionLoop *loop);

/**
 * Запустить полный цикл до достижения цели или ошибки
 */
int kolibri_ae_run_to_goal(KolibriActionLoop *loop, int max_steps);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_ACTION_ENGINE_H */
