/*
 * inference.c
 *
 * Реализация модуля инференса Kolibri AI
 *
 * Центральный pipeline вывода:
 *   query → кодирование → поиск знаний → рассуждение → генерация → ответ
 *
 * Поддерживаемые стратегии:
 *   DIRECT   — прямой поиск по ключевым словам
 *   FORMULA  — формульный вывод через KolibriFormulaPool
 *   LOGICAL  — рассуждение через мета-формулы
 *   CHAIN    — многошаговый chain-of-thought
 *   HYBRID   — комбинация всех методов с голосованием
 */

#include "kolibri/inference.h"
#include "kolibri/knowledge.h"
#include "kolibri/formula.h"
#include "kolibri/logical_memory.h"
#include "kolibri/formula_logic.h"
#include "kolibri/fractal_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

/* ========== Внутренние утилиты ========== */

/* Получить текущее время в миллисекундах */
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Простая токенизация (разбиение по пробелам) */
static size_t tokenize_query(const char *query, char tokens[][128], size_t max_tokens) {
    size_t count = 0;
    const char *p = query;

    while (*p && count < max_tokens) {
        /* Пропускаем пробелы */
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        if (!*p) break;

        size_t len = 0;
        while (p[len] && p[len] != ' ' && p[len] != '\t' && p[len] != '\n' && len < 127) {
            len++;
        }
        memcpy(tokens[count], p, len);
        tokens[count][len] = '\0';
        count++;
        p += len;
    }
    return count;
}

/* Подсчёт совпадений ключевых слов */
static double keyword_match_score(const char *text, char tokens[][128], size_t token_count) {
    if (!text || token_count == 0) return 0.0;
    size_t hits = 0;
    for (size_t i = 0; i < token_count; i++) {
        if (strstr(text, tokens[i])) hits++;
    }
    return (double)hits / (double)token_count;
}

/* ========== ЖИЗНЕННЫЙ ЦИКЛ ========== */

KolibriInferenceContext* kolibri_inference_create(void) {
    KolibriInferenceContext *ctx = calloc(1, sizeof(KolibriInferenceContext));
    if (!ctx) return NULL;

    ctx->strategy = KOLIBRI_INF_HYBRID;
    ctx->temperature = 0.7;
    ctx->max_steps = 8;
    ctx->knowledge_limit = 5;

    ctx->total_queries = 0;
    ctx->total_tokens_in = 0;
    ctx->total_tokens_out = 0;
    ctx->avg_confidence = 0.0;
    ctx->avg_duration_ms = 0.0;

    return ctx;
}

void kolibri_inference_destroy(KolibriInferenceContext *ctx) {
    free(ctx);  /* NULL-safe: free(NULL) определён */
}

int kolibri_inference_set_strategy(
    KolibriInferenceContext *ctx,
    KolibriInferenceStrategy strategy
) {
    if (!ctx) return -1;
    ctx->strategy = strategy;
    return 0;
}

int kolibri_inference_set_temperature(
    KolibriInferenceContext *ctx,
    double temperature
) {
    if (!ctx) return -1;
    if (temperature < 0.0) temperature = 0.0;
    if (temperature > 2.0) temperature = 2.0;
    ctx->temperature = temperature;
    return 0;
}

/* ========== ШАГИ ИНФЕРЕНСА ========== */

/*
 * Шаг 1: Прямой поиск по ключевым словам
 * Ищет в knowledge base документы, содержащие слова из запроса
 */
static int step_direct_search(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size,
    size_t *source_count
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Прямой поиск по ключевым словам");

    /* Токенизация запроса */
    char tokens[32][128];
    size_t tcount = tokenize_query(query, tokens, 32);

    /* Инициализируем knowledge index */
    KolibriKnowledgeIndex idx;
    if (kolibri_knowledge_index_init(&idx) != 0) {
        snprintf(step->result, sizeof(step->result), "Knowledge index unavailable");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    /* Загружаем знания из стандартного каталога */
    kolibri_knowledge_index_load_directory(&idx, "data");

    if (idx.count == 0) {
        snprintf(step->result, sizeof(step->result),
                 "No knowledge documents loaded");
        step->confidence = 0.1;
        step->duration_ms = now_ms() - t0;
        kolibri_knowledge_index_free(&idx);
        return 0;
    }

    /* Ищем лучшие совпадения */
    const KolibriKnowledgeDocument *results[16];
    double scores[16];
    size_t found = kolibri_knowledge_search_legacy(&idx, query, 5, results, scores);

    if (found > 0 && scores[0] > 0.0) {
        /* Компилируем ответ из лучших совпадений */
        size_t pos = 0;
        for (size_t i = 0; i < found && i < 3; i++) {
            int written = snprintf(partial_response + pos, partial_size - pos,
                                   "%s ", results[i]->content);
            if (written > 0) pos += (size_t)written;
        }
        *source_count = found;
        step->confidence = scores[0];
        snprintf(step->result, sizeof(step->result),
                 "Found %zu relevant documents (best score: %.2f)",
                 found, scores[0]);
    } else {
        snprintf(step->result, sizeof(step->result),
                 "No relevant documents found for query");
        step->confidence = 0.0;
    }

    kolibri_knowledge_index_free(&idx);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 2: Формульный вывод
 * Кодирует запрос в числовое представление и применяет формулы
 */
static int step_formula_inference(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Формульный вывод через числовые паттерны");

    /* Создаём formula pool на стеке / heap */
    KolibriFormulaPool *pool = calloc(1, sizeof(KolibriFormulaPool));
    if (!pool) {
        snprintf(step->result, sizeof(step->result), "Formula pool creation failed");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }
    kf_pool_init(pool, 42);

    /* Пытаемся найти ответ через формульный lookup */
    int query_hash = kf_hash_from_text(query);
    const KolibriFormula *best = kf_pool_best(pool);

    if (best) {
        char answer[512] = {0};
        int found = kf_formula_lookup_answer(best, query_hash, answer, sizeof(answer));
        if (found == 0 && strlen(answer) > 0) {
            snprintf(partial_response, partial_size, "%s", answer);
            step->confidence = 0.7;
            snprintf(step->result, sizeof(step->result),
                     "Formula match found (hash=%d)", query_hash);
        } else {
            snprintf(step->result, sizeof(step->result),
                     "No formula patterns matched (hash=%d)", query_hash);
            step->confidence = 0.0;
        }
    } else {
        snprintf(step->result, sizeof(step->result),
                 "No formulas in pool");
        step->confidence = 0.0;
    }

    kf_pool_free(pool);
    free(pool);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 3: Логическое рассуждение через мета-формулы
 * Использует logical_memory и formula_logic для вывода
 */
static int step_logical_reasoning(
    const char *query,
    KolibriInferenceStep *step,
    size_t *rules_fired
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Логическое рассуждение (мета-формулы)");

    MetaFormulaStore *store = mf_create_store();
    LogicalMemory *mem = lm_create_memory();
    if (!store || !mem) {
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        if (store) mf_destroy_store(store);
        if (mem) lm_destroy_memory(mem);
        return -1;
    }

    /* Сохраняем запрос как логическое выражение */
    lm_store_logic(mem, "query", lm_logic_constant(query));

    /* Запускаем автообнаружение паттернов */
    int discovered = mf_auto_discover_patterns(mem, store);
    *rules_fired = (size_t)(discovered > 0 ? discovered : 0);

    snprintf(step->result, sizeof(step->result),
             "Discovered %d patterns from query", discovered);
    step->confidence = discovered > 0 ? 0.5 : 0.1;

    mf_destroy_store(store);
    lm_destroy_memory(mem);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/*
 * Шаг 4: Фрактальная десятичная память
 * Кодирует запрос в десятичный путь и ищет ближайшие понятия
 */
static int step_fractal_memory(
    const char *query,
    KolibriInferenceStep *step,
    char *partial_response,
    size_t partial_size
) {
    double t0 = now_ms();
    snprintf(step->description, sizeof(step->description),
             "Поиск в фрактальной десятичной памяти");

    KfmContext fmem;
    if (kfm_init(&fmem, 42) != 0) {
        snprintf(step->result, sizeof(step->result),
                 "Fractal memory init failed");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }

    /* Кодируем запрос в десятичный путь */
    uint8_t query_path[KFM_MAX_DEPTH];
    size_t query_path_len = kfm_text_to_path(query, strlen(query),
                                              query_path, KFM_MAX_DEPTH);

    if (query_path_len == 0) {
        snprintf(step->result, sizeof(step->result),
                 "Query too short for fractal encoding");
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        kfm_free(&fmem);
        return 0;
    }

    /* Ограничиваем путь до разумной глубины */
    if (query_path_len > 30) query_path_len = 30;

    /* Вставляем запрос как понятие (для будущих обращений) */
    kfm_insert(&fmem, query_path, query_path_len, query, strlen(query));

    /* Ассоциативный поиск */
    KfmSearchResult results[5];
    int found = kfm_search(&fmem, query_path, query_path_len, results, 5);

    if (found > 0 && results[0].similarity > 0.3f) {
        /* Нашли релевантный путь — материализуем */
        if (results[0].node && results[0].node->payload_size > 0) {
            size_t copy_len = results[0].node->payload_size;
            if (copy_len >= partial_size) copy_len = partial_size - 1;
            memcpy(partial_response, results[0].node->payload, copy_len);
            partial_response[copy_len] = '\0';
        }
        step->confidence = (double)results[0].similarity;
        snprintf(step->result, sizeof(step->result),
                 "Found %d fractal paths (best sim: %.2f, depth: %u)",
                 found, results[0].similarity, results[0].path_len);
    } else {
        snprintf(step->result, sizeof(step->result),
                 "No matching fractal paths (query depth: %zu)",
                 query_path_len);
        step->confidence = 0.05;
    }

    /* Активируем найденный путь для укрепления ассоциаций */
    kfm_activate(&fmem, query_path, query_path_len, 0.5f);

    kfm_free(&fmem);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/* ========== ГЛАВНАЯ ФУНКЦИЯ ИНФЕРЕНСА ========== */

int kolibri_inference_run(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceResult *result
) {
    if (!ctx || !query || !result) return -1;

    memset(result, 0, sizeof(KolibriInferenceResult));
    double t_start = now_ms();

    /* Счётчик шагов */
    size_t step_idx = 0;

    /* Временные буферы для частичных ответов */
    char direct_response[4096] = {0};
    char formula_response[4096] = {0};
    size_t source_count = 0;
    size_t rules_fired = 0;

    /* === Шаг 1: Прямой поиск (если стратегия поддерживает) === */
    if (ctx->strategy == KOLIBRI_INF_DIRECT ||
        ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_direct_search(
            query,
            &result->steps[step_idx],
            direct_response,
            sizeof(direct_response),
            &source_count
        );
        step_idx++;
        result->knowledge_hits = source_count;
    }

    /* === Шаг 2: Формульный вывод === */
    if (ctx->strategy == KOLIBRI_INF_FORMULA ||
        ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_formula_inference(
            query,
            &result->steps[step_idx],
            formula_response,
            sizeof(formula_response)
        );
        step_idx++;
    }

    /* === Шаг 3: Логическое рассуждение === */
    if (ctx->strategy == KOLIBRI_INF_LOGICAL ||
        ctx->strategy == KOLIBRI_INF_HYBRID) {

        step_logical_reasoning(
            query,
            &result->steps[step_idx],
            &rules_fired
        );
        result->logic_rules_fired = rules_fired;
        step_idx++;
    }

    /* === Шаг 4: Фрактальная десятичная память === */
    char fractal_response[4096] = {0};
    if (ctx->strategy == KOLIBRI_INF_HYBRID ||
        ctx->strategy == KOLIBRI_INF_CHAIN) {

        step_fractal_memory(
            query,
            &result->steps[step_idx],
            fractal_response,
            sizeof(fractal_response)
        );
        step_idx++;
    }

    result->step_count = step_idx;

    /* === Финальная сборка ответа === */
    /* Приоритет: knowledge > formula > fractal > fallback */
    if (strlen(direct_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", direct_response);
    } else if (strlen(formula_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", formula_response);
    } else if (strlen(fractal_response) > 0) {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE, "%s", fractal_response);
    } else {
        snprintf(result->response, KOLIBRI_INF_MAX_RESPONSE,
                 "I don't have enough information to answer: \"%s\". "
                 "Searched %zu knowledge sources, applied %zu formulas, "
                 "fired %zu logic rules.",
                 query, source_count, result->formulas_applied, rules_fired);
    }
    result->response_length = strlen(result->response);

    /* Вычислить общую уверенность (взвешенное среднее шагов) */
    double sum_conf = 0.0;
    for (size_t i = 0; i < step_idx; i++) {
        sum_conf += result->steps[i].confidence;
    }
    result->total_confidence = step_idx > 0 ? sum_conf / (double)step_idx : 0.0;

    /* Время */
    result->total_duration_ms = now_ms() - t_start;

    /* Обновить статистику контекста */
    ctx->total_queries++;

    /* Подсчёт токенов (приблизительно по пробелам) */
    char tokens[64][128];
    ctx->total_tokens_in += tokenize_query(query, tokens, 64);
    ctx->total_tokens_out += tokenize_query(result->response, tokens, 64);

    /* Скользящее среднее уверенности и времени */
    double n = (double)ctx->total_queries;
    ctx->avg_confidence = ctx->avg_confidence * ((n - 1) / n)
                        + result->total_confidence / n;
    ctx->avg_duration_ms = ctx->avg_duration_ms * ((n - 1) / n)
                         + result->total_duration_ms / n;

    return 0;
}

/* ========== ОДИНОЧНЫЙ ШАГ ========== */

int kolibri_inference_step(
    KolibriInferenceContext *ctx,
    const char *query,
    KolibriInferenceStep *step
) {
    if (!ctx || !query || !step) return -1;

    memset(step, 0, sizeof(KolibriInferenceStep));
    double t0 = now_ms();

    snprintf(step->description, sizeof(step->description),
             "Single-step inference");

    /* Выполняем прямой поиск как единичный шаг */
    KolibriKnowledgeIndex idx;
    if (kolibri_knowledge_index_init(&idx) != 0) {
        step->confidence = 0.0;
        step->duration_ms = now_ms() - t0;
        return -1;
    }
    kolibri_knowledge_index_load_directory(&idx, "data");

    const KolibriKnowledgeDocument *results[8];
    double scores[8];
    size_t found = kolibri_knowledge_search_legacy(&idx, query, 3, results, scores);

    if (found > 0 && scores[0] > 0.0) {
        snprintf(step->result, sizeof(step->result), "%s",
                 results[0]->content ? results[0]->content : "");
        step->confidence = scores[0];
    } else {
        snprintf(step->result, sizeof(step->result), "No match");
        step->confidence = 0.0;
    }

    kolibri_knowledge_index_free(&idx);
    step->duration_ms = now_ms() - t0;
    return 0;
}

/* ========== СТАТИСТИКА ========== */

int kolibri_inference_get_stats(
    const KolibriInferenceContext *ctx,
    uint64_t *total_queries,
    double *avg_confidence,
    double *avg_duration
) {
    if (!ctx) return -1;
    if (total_queries) *total_queries = ctx->total_queries;
    if (avg_confidence) *avg_confidence = ctx->avg_confidence;
    if (avg_duration) *avg_duration = ctx->avg_duration_ms;
    return 0;
}

void kolibri_inference_reset_stats(KolibriInferenceContext *ctx) {
    if (!ctx) return;
    ctx->total_queries = 0;
    ctx->total_tokens_in = 0;
    ctx->total_tokens_out = 0;
    ctx->avg_confidence = 0.0;
    ctx->avg_duration_ms = 0.0;
}
