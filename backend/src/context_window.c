/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Context Window Implementation
 * Реализация контекстного окна с механизмом attention
 */

#include "kolibri/context.h"
#include "kolibri/random.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int k_context_window_init(KolibriContextWindow *ctx) {
    if (!ctx) return -1;
    
    memset(ctx->tokens, 0, sizeof(ctx->tokens));
    ctx->token_count = 0;
    ctx->current_position = 0;
    ctx->attention_matrix = NULL;
    ctx->attention_matrix_size = 0;
    
    return 0;
}

void k_context_window_free(KolibriContextWindow *ctx) {
    if (!ctx) return;
    
    if (ctx->attention_matrix) {
        free(ctx->attention_matrix);
        ctx->attention_matrix = NULL;
    }
    
    ctx->token_count = 0;
    ctx->attention_matrix_size = 0;
}

int k_context_window_add_token(KolibriContextWindow *ctx,
                               const char *text,
                               const KolibriSemanticPattern *pattern) {
    if (!ctx || !text) return -1;
    if (ctx->token_count >= KOLIBRI_CONTEXT_WINDOW_SIZE) return -1;
    
    /* Кодируем токен в цифры */
    size_t text_len = strlen(text);
    uint8_t buffer[1024];
    kolibri_potok_cifr stream;
    
    if (kolibri_potok_cifr_init(&stream, buffer, sizeof(buffer)) != 0) {
        return -1;
    }
    
    if (kolibri_transducirovat_utf8(&stream, (const uint8_t *)text, text_len) != 0) {
        return -1;
    }
    
    /* Добавляем токен */
    size_t idx = ctx->token_count;
    ctx->tokens[idx].digits = stream;
    
    if (pattern) {
        ctx->tokens[idx].pattern = *pattern;
    } else {
        k_semantic_pattern_init(&ctx->tokens[idx].pattern);
    }
    
    ctx->tokens[idx].attention_weight = 0.0;
    ctx->tokens[idx].position = idx;
    
    ctx->token_count++;
    
    return 0;
}

/**
 * Вычисление сходства между двумя числовыми потоками
 * Внутренняя функция для attention механизма
 */
static double compute_digit_similarity(const kolibri_potok_cifr *a,
                                      const kolibri_potok_cifr *b) {
    if (!a || !b) return 0.0;
    
    size_t min_len = a->dlina < b->dlina ? a->dlina : b->dlina;
    if (min_len == 0) return 0.0;
    
    size_t matches = 0;
    for (size_t i = 0; i < min_len; i++) {
        if (a->danniye[i] == b->danniye[i]) {
            matches++;
        }
    }
    
    return (double)matches / (double)min_len;
}

/**
 * Вычисление softmax для нормализации весов внимания
 */
static void compute_softmax(double *values, size_t count) {
    if (!values || count == 0) return;
    
    /* Находим максимум для численной стабильности */
    double max_val = values[0];
    for (size_t i = 1; i < count; i++) {
        if (values[i] > max_val) {
            max_val = values[i];
        }
    }
    
    /* Вычисляем exp и сумму */
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        values[i] = exp(values[i] - max_val);
        sum += values[i];
    }
    
    /* Нормализуем */
    if (sum > 0.0) {
        for (size_t i = 0; i < count; i++) {
            values[i] /= sum;
        }
    }
}

int k_context_window_compute_attention(KolibriContextWindow *ctx) {
    if (!ctx || ctx->token_count == 0) return -1;
    
    /* Выделяем память для матрицы внимания если нужно */
    size_t matrix_size = ctx->token_count * ctx->token_count;
    if (ctx->attention_matrix_size < matrix_size) {
        double *new_matrix = (double *)realloc(ctx->attention_matrix,
                                              matrix_size * sizeof(double));
        if (!new_matrix) return -1;
        
        ctx->attention_matrix = new_matrix;
        ctx->attention_matrix_size = matrix_size;
    }
    
    /* Вычисляем попарные веса внимания */
    for (size_t i = 0; i < ctx->token_count; i++) {
        for (size_t j = 0; j < ctx->token_count; j++) {
            double similarity = 0.0;
            
            /* Сходство на основе числовых потоков */
            similarity += compute_digit_similarity(&ctx->tokens[i].digits,
                                                  &ctx->tokens[j].digits);
            
            /* Сходство на основе семантических паттернов */
            double pattern_sim = k_semantic_similarity(&ctx->tokens[i].pattern,
                                                      &ctx->tokens[j].pattern);
            similarity += pattern_sim;
            
            /* Учитываем позиционную близость */
            double pos_distance = (double)abs((int)i - (int)j);
            double pos_weight = 1.0 / (1.0 + pos_distance * 0.1);
            similarity *= pos_weight;
            
            ctx->attention_matrix[i * ctx->token_count + j] = similarity;
        }
        
        /* Применяем softmax к строке матрицы */
        compute_softmax(&ctx->attention_matrix[i * ctx->token_count],
                       ctx->token_count);
    }
    
    /* Обновляем веса внимания для каждого токена */
    for (size_t i = 0; i < ctx->token_count; i++) {
        double total_attention = 0.0;
        for (size_t j = 0; j < ctx->token_count; j++) {
            total_attention += ctx->attention_matrix[i * ctx->token_count + j];
        }
        ctx->tokens[i].attention_weight = total_attention / (double)ctx->token_count;
    }
    
    return 0;
}

const KolibriContextToken *k_context_window_get_token(const KolibriContextWindow *ctx,
                                                     size_t position) {
    if (!ctx || position >= ctx->token_count) return NULL;
    return &ctx->tokens[position];
}

double k_context_window_get_attention(const KolibriContextWindow *ctx,
                                     size_t from_pos,
                                     size_t to_pos) {
    if (!ctx || !ctx->attention_matrix) return -1.0;
    if (from_pos >= ctx->token_count || to_pos >= ctx->token_count) return -1.0;
    
    return ctx->attention_matrix[from_pos * ctx->token_count + to_pos];
}

/**
 * Структура для сортировки токенов по релевантности
 */
typedef struct {
    size_t position;
    double relevance;
} TokenRelevance;

/**
 * Функция сравнения для qsort (по убыванию релевантности)
 */
static int compare_relevance(const void *a, const void *b) {
    const TokenRelevance *ta = (const TokenRelevance *)a;
    const TokenRelevance *tb = (const TokenRelevance *)b;
    
    if (ta->relevance > tb->relevance) return -1;
    if (ta->relevance < tb->relevance) return 1;
    return 0;
}

int k_context_window_extract_relevant(const KolibriContextWindow *ctx,
                                      size_t query_position,
                                      size_t top_k,
                                      size_t *result) {
    if (!ctx || !result || query_position >= ctx->token_count) return -1;
    if (!ctx->attention_matrix) return -1;
    if (top_k == 0 || top_k > ctx->token_count) return -1;
    
    /* Создаём массив для сортировки */
    TokenRelevance *relevances = (TokenRelevance *)malloc(ctx->token_count * sizeof(TokenRelevance));
    if (!relevances) return -1;
    
    /* Заполняем релевантности */
    for (size_t i = 0; i < ctx->token_count; i++) {
        relevances[i].position = i;
        relevances[i].relevance = ctx->attention_matrix[query_position * ctx->token_count + i];
    }
    
    /* Сортируем по релевантности */
    qsort(relevances, ctx->token_count, sizeof(TokenRelevance), compare_relevance);
    
    /* Извлекаем top_k */
    size_t extracted = top_k < ctx->token_count ? top_k : ctx->token_count;
    for (size_t i = 0; i < extracted; i++) {
        result[i] = relevances[i].position;
    }
    
    free(relevances);
    return (int)extracted;
}

void k_context_window_reset(KolibriContextWindow *ctx) {
    if (!ctx) return;
    
    ctx->token_count = 0;
    ctx->current_position = 0;
    
    if (ctx->attention_matrix) {
        memset(ctx->attention_matrix, 0, ctx->attention_matrix_size * sizeof(double));
    }
}

int k_context_window_slide(KolibriContextWindow *ctx, size_t keep_last) {
    if (!ctx) return -1;
    if (keep_last >= ctx->token_count) return 0; /* Ничего не делаем */
    
    /* Сдвигаем токены */
    size_t shift = ctx->token_count - keep_last;
    memmove(&ctx->tokens[0], &ctx->tokens[shift],
            keep_last * sizeof(KolibriContextToken));
    
    /* Обновляем позиции */
    for (size_t i = 0; i < keep_last; i++) {
        ctx->tokens[i].position = i;
    }
    
    ctx->token_count = keep_last;
    ctx->current_position = keep_last;
    
    /* Пересчитываем attention после сдвига */
    return k_context_window_compute_attention(ctx);
}

int k_context_window_serialize(const KolibriContextWindow *ctx,
                               kolibri_potok_cifr *stream) {
    if (!ctx || !stream) return -1;
    
    /* Сериализуем количество токенов (3 цифры) */
    size_t count = ctx->token_count;
    if (count > 999) count = 999; /* Ограничение */
    
    uint8_t count_digits[3];
    count_digits[0] = (count / 100) % 10;
    count_digits[1] = (count / 10) % 10;
    count_digits[2] = count % 10;
    
    for (int i = 0; i < 3; i++) {
        if (kolibri_potok_cifr_push(stream, count_digits[i]) != 0) {
            return -1;
        }
    }
    
    /* Сериализуем каждый токен (только паттерны) */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < KOLIBRI_SEMANTIC_PATTERN_SIZE; j++) {
            if (kolibri_potok_cifr_push(stream, ctx->tokens[i].pattern.pattern[j]) != 0) {
                return -1;
            }
        }
    }
    
    return 0;
}

int k_context_window_deserialize(KolibriContextWindow *ctx,
                                 const kolibri_potok_cifr *stream) {
    if (!ctx || !stream || stream->dlina < 3) return -1;
    
    /* Десериализуем количество токенов */
    size_t count = stream->danniye[0] * 100 +
                   stream->danniye[1] * 10 +
                   stream->danniye[2];
    
    if (count > KOLIBRI_CONTEXT_WINDOW_SIZE) return -1;
    
    size_t expected_size = 3 + count * KOLIBRI_SEMANTIC_PATTERN_SIZE;
    if (stream->dlina < expected_size) return -1;
    
    /* Десериализуем токены */
    k_context_window_reset(ctx);
    size_t pos = 3;
    
    for (size_t i = 0; i < count; i++) {
        k_semantic_pattern_init(&ctx->tokens[i].pattern);
        
        for (size_t j = 0; j < KOLIBRI_SEMANTIC_PATTERN_SIZE; j++) {
            ctx->tokens[i].pattern.pattern[j] = stream->danniye[pos++];
        }
        
        ctx->tokens[i].position = i;
        ctx->tokens[i].attention_weight = 0.0;
    }
    
    ctx->token_count = count;
    
    return 0;
}
