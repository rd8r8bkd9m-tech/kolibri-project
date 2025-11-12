/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Text Generation Module
 * Генерация текста через числовые паттерны с компрессией формулами
 */

#ifndef KOLIBRI_GENERATION_H
#define KOLIBRI_GENERATION_H

#include "kolibri/context.h"
#include "kolibri/corpus.h"
#include "kolibri/formula.h"
#include "kolibri/semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Размер beam для beam search */
#define KOLIBRI_BEAM_SIZE 10

/* Максимальная длина генерируемой последовательности */
#define KOLIBRI_MAX_GENERATION_LENGTH 1024

/**
 * Стратегия генерации
 */
typedef enum {
    KOLIBRI_GEN_GREEDY,      /* Жадная генерация (всегда лучший) */
    KOLIBRI_GEN_BEAM,        /* Beam search */
    KOLIBRI_GEN_SAMPLING,    /* Случайная выборка с temperature */
    KOLIBRI_GEN_FORMULA      /* Генерация через эволюцию формул (КОМПРЕССИЯ!) */
} KolibriGenerationStrategy;

/**
 * Кандидат для генерации (в beam search)
 */
typedef struct {
    char token[128];                /* Токен-кандидат */
    KolibriSemanticPattern pattern; /* Семантический паттерн */
    double score;                   /* Оценка качества */
    double formula_compression;     /* Степень компрессии формулой */
} KolibriGenerationCandidate;

/**
 * Контекст генерации текста
 */
typedef struct {
    KolibriCorpusContext *corpus;       /* Корпус с изученными паттернами */
    KolibriContextWindow *context;      /* Контекстное окно */
    KolibriFormulaPool *formula_pool;   /* Пул формул для компрессии */
    
    KolibriGenerationStrategy strategy; /* Стратегия генерации */
    double temperature;                 /* Temperature для sampling (0.0-2.0) */
    size_t beam_size;                   /* Размер beam */
    size_t max_length;                  /* Максимальная длина генерации */
    
    /* Статистика генерации */
    size_t tokens_generated;
    size_t formulas_used;               /* Сколько раз использованы формулы */
    double avg_compression_ratio;       /* Средняя степень компрессии */
    double generation_time_sec;
} KolibriGenerationContext;

/**
 * Инициализация контекста генерации
 * 
 * @param ctx Контекст генерации
 * @param corpus Корпус с изученными паттернами
 * @param strategy Стратегия генерации
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_gen_init(KolibriGenerationContext *ctx,
               KolibriCorpusContext *corpus,
               KolibriGenerationStrategy strategy);

/**
 * Освобождение ресурсов контекста генерации
 * 
 * @param ctx Контекст генерации
 */
void k_gen_free(KolibriGenerationContext *ctx);

/**
 * Генерация следующего токена на основе контекста
 * 
 * @param ctx Контекст генерации
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_gen_next_token(KolibriGenerationContext *ctx,
                     char *output,
                     size_t output_size);

/**
 * Генерация текста заданной длины
 * 
 * @param ctx Контекст генерации
 * @param prompt Начальный текст (может быть NULL)
 * @param num_tokens Количество токенов для генерации
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return Количество сгенерированных токенов или -1 при ошибке
 */
int k_gen_generate(KolibriGenerationContext *ctx,
                   const char *prompt,
                   size_t num_tokens,
                   char *output,
                   size_t output_size);

/**
 * КЛЮЧЕВАЯ ФУНКЦИЯ: Компрессия паттерна через формулу
 * Использует эволюцию формул для нахождения компактного представления
 * 
 * @param ctx Контекст генерации
 * @param pattern Паттерн для компрессии
 * @param formula Результирующая формула (выход)
 * @return Степень компрессии (> 1.0 = хорошо) или -1.0 при ошибке
 */
double k_gen_compress_pattern(KolibriGenerationContext *ctx,
                              const KolibriSemanticPattern *pattern,
                              KolibriFormula *formula);

/**
 * Декомпрессия паттерна из формулы
 * 
 * @param ctx Контекст генерации
 * @param formula Формула
 * @param pattern Восстановленный паттерн (выход)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_gen_decompress_pattern(KolibriGenerationContext *ctx,
                             const KolibriFormula *formula,
                             KolibriSemanticPattern *pattern);

/**
 * Beam search генерация с использованием формул
 * 
 * @param ctx Контекст генерации
 * @param candidates Массив кандидатов (размер = beam_size)
 * @param num_candidates Количество кандидатов (выход)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_gen_beam_search(KolibriGenerationContext *ctx,
                      KolibriGenerationCandidate *candidates,
                      size_t *num_candidates);

/**
 * Эволюционная генерация через формулы
 * Самая инновационная стратегия - использует формулы для эволюции паттернов
 * 
 * @param ctx Контекст генерации
 * @param generations Количество поколений эволюции
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_gen_evolve_text(KolibriGenerationContext *ctx,
                      size_t generations,
                      char *output,
                      size_t output_size);

/**
 * Вычисление perplexity для оценки качества
 * 
 * @param ctx Контекст генерации
 * @param text Текст для оценки
 * @param text_len Длина текста
 * @return Perplexity (меньше = лучше) или -1.0 при ошибке
 */
double k_gen_perplexity(KolibriGenerationContext *ctx,
                       const char *text,
                       size_t text_len);

/**
 * Оценка семантической когерентности через паттерны
 * 
 * @param ctx Контекст генерации
 * @param text Текст для оценки
 * @param text_len Длина текста
 * @return Оценка когерентности (0.0-1.0) или -1.0 при ошибке
 */
double k_gen_coherence(KolibriGenerationContext *ctx,
                      const char *text,
                      size_t text_len);

/**
 * Настройка temperature для sampling
 * 
 * @param ctx Контекст генерации
 * @param temperature Temperature (0.0 = детерминированно, 2.0 = случайно)
 */
void k_gen_set_temperature(KolibriGenerationContext *ctx, double temperature);

/**
 * Настройка размера beam
 * 
 * @param ctx Контекст генерации
 * @param beam_size Размер beam (1-100)
 */
void k_gen_set_beam_size(KolibriGenerationContext *ctx, size_t beam_size);

/**
 * Получение статистики генерации
 * 
 * @param ctx Контекст генерации
 * @param tokens_generated Количество сгенерированных токенов (выход)
 * @param formulas_used Количество использованных формул (выход)
 * @param compression_ratio Средняя степень компрессии (выход)
 */
void k_gen_get_stats(const KolibriGenerationContext *ctx,
                    size_t *tokens_generated,
                    size_t *formulas_used,
                    double *compression_ratio);

/**
 * Вывод статистики генерации
 * 
 * @param ctx Контекст генерации
 */
void k_gen_print_stats(const KolibriGenerationContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_GENERATION_H */
