/*
 * reasoning.c — Высокоуровневый интерфейс движка рассуждений Kolibri
 *
 * Этот модуль является фасадом над core/reasoning_engine.c и обеспечивает
 * унифицированный доступ к логическому выводу для WASM и других интерфейсов.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reasoning.h"
#include "kolibri/reasoning_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Глобальная конфигурация фасадного движка */
static KolibriREConfig g_reasoning_config;
static int g_reasoning_initialized = 0;

/* Вспомогательная функция эскейпинга JSON */
static void escape_json_string(const char* src, char* dst, size_t dst_len) {
    if (!src || !dst || dst_len == 0) return;
    size_t i = 0, j = 0;
    while (src[i] && j < dst_len - 2) {
        switch (src[i]) {
            case '"': case '\\':
                if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = src[i]; }
                break;
            case '\n': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 'n'; } break;
            case '\r': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 'r'; } break;
            case '\t': if (j < dst_len - 3) { dst[j++] = '\\'; dst[j++] = 't'; } break;
            default: dst[j++] = src[i]; break;
        }
        i++;
    }
    dst[j] = '\0';
}

/**
 * Инициализация системы рассуждений с параметрами по умолчанию
 */
int kolibri_reasoning_init(void) {
    if (g_reasoning_initialized) return 0;

    memset(&g_reasoning_config, 0, sizeof(g_reasoning_config));
    g_reasoning_config.enable_deductive = 1;
    g_reasoning_config.enable_inductive = 1;
    g_reasoning_config.enable_abductive = 1;
    g_reasoning_config.enable_analogical = 1;
    g_reasoning_config.enable_counterfactual = 1;
    g_reasoning_config.min_confidence_threshold = 0.5;
    g_reasoning_config.max_chain_length = 16;
    g_reasoning_config.max_hypotheses = 8;
    g_reasoning_config.verbose = 0;

    int status = kolibri_re_init(&g_reasoning_config);
    if (status == 0) {
        g_reasoning_initialized = 1;
    }
    return status;
}

/**
 * Получить указатель на глобальную конфигурацию рассуждений
 */
KolibriREConfig* kolibri_reasoning_get_config(void) {
    if (!g_reasoning_initialized) kolibri_reasoning_init();
    return &g_reasoning_config;
}

/**
 * Выполнение рассуждения по текстовому запросу
 */
int kolibri_reasoning_query(const char *query, char *out_answer, size_t out_capacity) {
    if (!query || !out_answer) return -1;
    if (!g_reasoning_initialized) kolibri_reasoning_init();

    KolibriReasoningResult result;
    memset(&result, 0, sizeof(result));

    int status = kolibri_re_reason(query, &g_reasoning_config, &result, NULL, NULL);

    if (status == 0) {
        strncpy(out_answer, result.answer, out_capacity - 1);
    } else {
        snprintf(out_answer, out_capacity, "Ошибка рассуждения: %d", status);
    }

    return status;
}

/**
 * Выполнение рассуждения и возврат результата в формате JSON с цепочкой
 */
int kolibri_reasoning_query_json(const char *query, char *out_json, size_t out_capacity) {
    if (!query || !out_json || out_capacity == 0) return -1;
    if (!g_reasoning_initialized) kolibri_reasoning_init();

    KolibriReasoningResult result;
    memset(&result, 0, sizeof(result));

    int status = kolibri_re_reason(query, &g_reasoning_config, &result, NULL, NULL);

    char esc_answer[4096] = {0};
    escape_json_string(status == 0 ? result.answer : "Failed to reason.", esc_answer, sizeof(esc_answer));

    size_t pos = 0;
    pos += snprintf(out_json + pos, out_capacity - pos, "{\"status\": %d, \"confidence\": %.2f, \"time_ms\": %.1f, \"answer\": \"%s\", \"chain\": [",
                    status, result.confidence, result.reasoning_time_ms, esc_answer);

    for (int i = 0; i < result.chain.num_steps && pos < out_capacity; i++) {
        char esc_premise[1024] = {0};
        char esc_conclusion[1024] = {0};
        char esc_desc[1024] = {0};

        escape_json_string(result.chain.steps[i].premise, esc_premise, sizeof(esc_premise));
        escape_json_string(result.chain.steps[i].conclusion, esc_conclusion, sizeof(esc_conclusion));
        escape_json_string(result.chain.steps[i].description, esc_desc, sizeof(esc_desc));

        int written = snprintf(out_json + pos, out_capacity - pos,
                 "%s{\"step\": %d, \"type\": %d, \"desc\": \"%s\", \"premise\": \"%s\", \"conclusion\": \"%s\", \"confidence\": %.2f}",
                 i > 0 ? ", " : "",
                 result.chain.steps[i].step_num,
                 result.chain.steps[i].type,
                 esc_desc, esc_premise, esc_conclusion,
                 result.chain.steps[i].confidence);

        if (written > 0 && (size_t)written < out_capacity - pos) {
            pos += written;
        } else {
            break;
        }
    }

    if (pos < out_capacity) {
        snprintf(out_json + pos, out_capacity - pos, "]}");
    }

    return status;
}

/**
 * Выполнение counterfactual рассуждения ("что если")
 */
int kolibri_reasoning_counterfactual(const char *query, const char *what_if, char *out_answer, size_t out_capacity) {
    if (!query || !what_if || !out_answer) return -1;
    if (!g_reasoning_initialized) kolibri_reasoning_init();

    KolibriReasoningResult result;
    memset(&result, 0, sizeof(result));

    int status = kolibri_re_counterfactual(query, what_if, &g_reasoning_config, &result);
    if (status == 0) {
        snprintf(out_answer, out_capacity, "%s (Если бы: %s -> То: %s)",
                 result.answer, result.counterfactual_premise, result.counterfactual_outcome);
    } else {
        snprintf(out_answer, out_capacity, "Ошибка counterfactual рассуждения: %d", status);
    }
    return status;
}

/**
 * Выполнение абдуктивного рассуждения
 */
int kolibri_reasoning_abductive(const char *query, char *out_answer, size_t out_capacity) {
    if (!query || !out_answer) return -1;
    if (!g_reasoning_initialized) kolibri_reasoning_init();

    KolibriReasoningResult result;
    memset(&result, 0, sizeof(result));

    int status = kolibri_re_abductive(query, &g_reasoning_config, &result);
    if (status == 0) {
        if (result.num_hypotheses > 0 && result.best_hypothesis_idx >= 0) {
            snprintf(out_answer, out_capacity, "%s (Лучшая гипотеза: %s, вероятность: %.2f)",
                     result.answer,
                     result.hypotheses[result.best_hypothesis_idx].hypothesis,
                     result.hypotheses[result.best_hypothesis_idx].probability);
        } else {
            strncpy(out_answer, result.answer, out_capacity - 1);
        }
    } else {
        snprintf(out_answer, out_capacity, "Ошибка абдуктивного рассуждения: %d", status);
    }
    return status;
}
