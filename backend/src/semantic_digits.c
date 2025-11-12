/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Semantic Digits Implementation
 * Реализация кодирования смысла через числовые паттерны
 */

#include "kolibri/semantic.h"
#include "kolibri/digits.h"
#include "kolibri/random.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void k_semantic_pattern_init(KolibriSemanticPattern *pattern) {
    if (!pattern) return;
    
    memset(pattern->pattern, 0, sizeof(pattern->pattern));
    pattern->context_weight = 0.0;
    pattern->usage_count = 0;
    memset(pattern->word, 0, sizeof(pattern->word));
}

void k_semantic_pattern_free(KolibriSemanticPattern *pattern) {
    if (!pattern) return;
    /* В текущей версии нет динамических выделений */
}

int k_semantic_context_init(KolibriSemanticContext *ctx) {
    if (!ctx) return -1;
    
    ctx->context_count = 0;
    memset(ctx->relevance, 0, sizeof(ctx->relevance));
    
    return 0;
}

void k_semantic_context_free(KolibriSemanticContext *ctx) {
    if (!ctx) return;
    /* Освобождение ресурсов при необходимости */
}

int k_semantic_context_add_word(KolibriSemanticContext *ctx,
                                const char *word,
                                double relevance) {
    if (!ctx || !word) return -1;
    if (ctx->context_count >= KOLIBRI_SEMANTIC_CONTEXT_MAX) return -1;
    
    /* Кодируем слово в цифры */
    size_t word_len = strlen(word);
    uint8_t buffer[1024];
    kolibri_potok_cifr stream;
    
    if (kolibri_potok_cifr_init(&stream, buffer, sizeof(buffer)) != 0) {
        return -1;
    }
    
    if (kolibri_transducirovat_utf8(&stream, (const uint8_t *)word, word_len) != 0) {
        return -1;
    }
    
    /* Сохраняем в контекст */
    size_t idx = ctx->context_count;
    ctx->context_words[idx] = stream;
    ctx->relevance[idx] = relevance;
    ctx->context_count++;
    
    return 0;
}

/**
 * Внутренняя функция: вычисление fitness паттерна
 * Чем лучше паттерн связывает слово с контекстом, тем выше fitness
 */
static double compute_pattern_fitness(const uint8_t *pattern,
                                     size_t pattern_len,
                                     const kolibri_potok_cifr *word_digits,
                                     const KolibriSemanticContext *ctx) {
    if (!pattern || !word_digits || !ctx) return 0.0;
    
    double total_fitness = 0.0;
    
    /* Для каждого слова в контексте */
    for (size_t i = 0; i < ctx->context_count; i++) {
        const kolibri_potok_cifr *context_word = &ctx->context_words[i];
        double relevance = ctx->relevance[i];
        
        /* Вычисляем, насколько паттерн связывает основное слово с контекстным */
        double match_score = 0.0;
        
        /* Простая метрика: совпадение паттерна с началом контекстного слова */
        size_t min_len = pattern_len < context_word->dlina ? pattern_len : context_word->dlina;
        size_t matches = 0;
        
        for (size_t j = 0; j < min_len; j++) {
            if (pattern[j] == context_word->danniye[j]) {
                matches++;
            }
        }
        
        match_score = (double)matches / (double)min_len;
        total_fitness += match_score * relevance;
    }
    
    /* Нормализуем */
    if (ctx->context_count > 0) {
        total_fitness /= (double)ctx->context_count;
    }
    
    return total_fitness;
}

/**
 * Внутренняя функция: мутация паттерна
 */
static void mutate_pattern(uint8_t *pattern, size_t len, KolibriRng *rng) {
    if (!pattern || !rng || len == 0) return;
    
    /* Выбираем случайную позицию */
    size_t pos = k_rng_next(rng) % len;
    
    /* Меняем цифру на случайную 0-9 */
    pattern[pos] = (uint8_t)(k_rng_next(rng) % 10);
}

/**
 * Внутренняя функция: кроссовер двух паттернов
 */
static void crossover_patterns(const uint8_t *p1, const uint8_t *p2,
                              uint8_t *offspring, size_t len, KolibriRng *rng) {
    if (!p1 || !p2 || !offspring || !rng || len == 0) return;
    
    /* Точка разреза */
    size_t crossover_point = k_rng_next(rng) % len;
    
    /* Копируем части */
    memcpy(offspring, p1, crossover_point);
    memcpy(offspring + crossover_point, p2 + crossover_point, len - crossover_point);
}

int k_semantic_learn(const char *word,
                     const KolibriSemanticContext *ctx,
                     size_t generations,
                     KolibriSemanticPattern *pattern) {
    if (!word || !ctx || !pattern) return -1;
    if (generations == 0) generations = 1000; /* По умолчанию */
    
    /* Инициализируем паттерн */
    k_semantic_pattern_init(pattern);
    strncpy(pattern->word, word, sizeof(pattern->word) - 1);
    
    /* Кодируем слово в цифры */
    size_t word_len = strlen(word);
    uint8_t word_buffer[1024];
    kolibri_potok_cifr word_stream;
    
    if (kolibri_potok_cifr_init(&word_stream, word_buffer, sizeof(word_buffer)) != 0) {
        return -1;
    }
    
    if (kolibri_transducirovat_utf8(&word_stream, (const uint8_t *)word, word_len) != 0) {
        return -1;
    }
    
    /* Инициализируем популяцию паттернов */
    #define POPULATION_SIZE 50
    uint8_t population[POPULATION_SIZE][KOLIBRI_SEMANTIC_PATTERN_SIZE];
    double fitness[POPULATION_SIZE];
    
    KolibriRng rng;
    k_rng_seed(&rng, (uint64_t)time(NULL));
    
    /* Случайная инициализация популяции */
    for (size_t i = 0; i < POPULATION_SIZE; i++) {
        for (size_t j = 0; j < KOLIBRI_SEMANTIC_PATTERN_SIZE; j++) {
            population[i][j] = (uint8_t)(k_rng_next(&rng) % 10);
        }
    }
    
    /* Эволюция */
    for (size_t gen = 0; gen < generations; gen++) {
        /* Оцениваем fitness каждого паттерна */
        for (size_t i = 0; i < POPULATION_SIZE; i++) {
            fitness[i] = compute_pattern_fitness(population[i],
                                                 KOLIBRI_SEMANTIC_PATTERN_SIZE,
                                                 &word_stream, ctx);
        }
        
        /* Сортируем по fitness (простая сортировка пузырьком) */
        for (size_t i = 0; i < POPULATION_SIZE - 1; i++) {
            for (size_t j = 0; j < POPULATION_SIZE - i - 1; j++) {
                if (fitness[j] < fitness[j + 1]) {
                    /* Меняем местами */
                    double temp_fitness = fitness[j];
                    fitness[j] = fitness[j + 1];
                    fitness[j + 1] = temp_fitness;
                    
                    uint8_t temp_pattern[KOLIBRI_SEMANTIC_PATTERN_SIZE];
                    memcpy(temp_pattern, population[j], KOLIBRI_SEMANTIC_PATTERN_SIZE);
                    memcpy(population[j], population[j + 1], KOLIBRI_SEMANTIC_PATTERN_SIZE);
                    memcpy(population[j + 1], temp_pattern, KOLIBRI_SEMANTIC_PATTERN_SIZE);
                }
            }
        }
        
        /* Размножение: верхняя половина создаёт потомков */
        size_t elite_size = POPULATION_SIZE / 2;
        for (size_t i = elite_size; i < POPULATION_SIZE; i++) {
            /* Выбираем двух родителей из элиты */
            size_t parent1 = k_rng_next(&rng) % elite_size;
            size_t parent2 = k_rng_next(&rng) % elite_size;
            
            /* Кроссовер */
            crossover_patterns(population[parent1], population[parent2],
                             population[i], KOLIBRI_SEMANTIC_PATTERN_SIZE, &rng);
            
            /* Мутация с вероятностью 10% */
            if ((k_rng_next(&rng) % 100) < 10) {
                mutate_pattern(population[i], KOLIBRI_SEMANTIC_PATTERN_SIZE, &rng);
            }
        }
    }
    
    /* Лучший паттерн - это результат */
    memcpy(pattern->pattern, population[0], KOLIBRI_SEMANTIC_PATTERN_SIZE);
    pattern->context_weight = fitness[0];
    pattern->usage_count = 1;
    
    return 0;
}

double k_semantic_similarity(const KolibriSemanticPattern *p1,
                            const KolibriSemanticPattern *p2) {
    if (!p1 || !p2) return 0.0;
    
    /* Вычисляем процент совпадающих цифр */
    size_t matches = 0;
    for (size_t i = 0; i < KOLIBRI_SEMANTIC_PATTERN_SIZE; i++) {
        if (p1->pattern[i] == p2->pattern[i]) {
            matches++;
        }
    }
    
    return (double)matches / (double)KOLIBRI_SEMANTIC_PATTERN_SIZE;
}

int k_semantic_find_nearest(const KolibriSemanticPattern *pattern,
                            const KolibriSemanticPattern *candidates,
                            size_t count) {
    if (!pattern || !candidates || count == 0) return -1;
    
    int best_idx = -1;
    double best_similarity = -1.0;
    
    for (size_t i = 0; i < count; i++) {
        double sim = k_semantic_similarity(pattern, &candidates[i]);
        if (sim > best_similarity) {
            best_similarity = sim;
            best_idx = (int)i;
        }
    }
    
    return best_idx;
}

int k_semantic_merge_patterns(const KolibriSemanticPattern *p1,
                              const KolibriSemanticPattern *p2,
                              KolibriSemanticPattern *merged) {
    if (!p1 || !p2 || !merged) return -1;
    
    /* Простое слияние: усредняем цифры */
    for (size_t i = 0; i < KOLIBRI_SEMANTIC_PATTERN_SIZE; i++) {
        uint16_t avg = ((uint16_t)p1->pattern[i] + (uint16_t)p2->pattern[i]) / 2;
        merged->pattern[i] = (uint8_t)avg;
    }
    
    /* Усредняем веса */
    merged->context_weight = (p1->context_weight + p2->context_weight) / 2.0;
    merged->usage_count = p1->usage_count + p2->usage_count;
    
    return 0;
}

double k_semantic_validate(const KolibriSemanticPattern *pattern,
                          const KolibriSemanticContext *ctx) {
    if (!pattern || !ctx) return 0.0;
    
    /* Пересчитываем fitness паттерна с текущим контекстом */
    uint8_t word_buffer[1024];
    kolibri_potok_cifr word_stream;
    
    if (kolibri_potok_cifr_init(&word_stream, word_buffer, sizeof(word_buffer)) != 0) {
        return 0.0;
    }
    
    /* Кодируем слово из паттерна */
    if (kolibri_transducirovat_utf8(&word_stream, (const uint8_t *)pattern->word,
                                    strlen(pattern->word)) != 0) {
        return 0.0;
    }
    
    return compute_pattern_fitness(pattern->pattern,
                                   KOLIBRI_SEMANTIC_PATTERN_SIZE,
                                   &word_stream, ctx);
}
