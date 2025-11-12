/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Semantic Digits Module
 * Кодирование смысла слов через числовые паттерны
 */

#ifndef KOLIBRI_SEMANTIC_H
#define KOLIBRI_SEMANTIC_H

#include "kolibri/decimal.h"
#include "kolibri/digits.h"
#include "kolibri/formula.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Максимальная длина семантического паттерна */
#define KOLIBRI_SEMANTIC_PATTERN_SIZE 64

/* Максимальное количество связанных слов в контексте */
#define KOLIBRI_SEMANTIC_CONTEXT_MAX 32

/**
 * Семантический паттерн - числовое представление смысла слова
 */
typedef struct {
    uint8_t pattern[KOLIBRI_SEMANTIC_PATTERN_SIZE]; /* Числовой паттерн 0-9 */
    double context_weight;                           /* Вес в текущем контексте */
    size_t usage_count;                              /* Частота использования */
    char word[128];                                  /* Само слово (для отладки) */
} KolibriSemanticPattern;

/**
 * Контекст для семантического обучения
 */
typedef struct {
    kolibri_potok_cifr word_digits;                      /* Цифры основного слова */
    kolibri_potok_cifr context_words[KOLIBRI_SEMANTIC_CONTEXT_MAX]; /* Окружающие слова */
    size_t context_count;                            /* Количество слов в контексте */
    double relevance[KOLIBRI_SEMANTIC_CONTEXT_MAX]; /* Релевантность каждого слова */
} KolibriSemanticContext;

/**
 * Инициализация семантического паттерна
 */
void k_semantic_pattern_init(KolibriSemanticPattern *pattern);

/**
 * Освобождение ресурсов паттерна
 */
void k_semantic_pattern_free(KolibriSemanticPattern *pattern);

/**
 * Инициализация семантического контекста
 */
int k_semantic_context_init(KolibriSemanticContext *ctx);

/**
 * Освобождение ресурсов контекста
 */
void k_semantic_context_free(KolibriSemanticContext *ctx);

/**
 * Добавление слова в контекст
 * 
 * @param ctx Контекст
 * @param word Слово для добавления
 * @param relevance Релевантность слова (0.0 - 1.0)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_semantic_context_add_word(KolibriSemanticContext *ctx, 
                                const char *word,
                                double relevance);

/**
 * Эволюционное обучение семантического паттерна
 * 
 * Использует эволюционный алгоритм для поиска числового паттерна,
 * который наилучшим образом связывает слово с его контекстом.
 * 
 * @param word Слово для обучения
 * @param ctx Контекст (окружающие слова)
 * @param generations Количество поколений эволюции
 * @param pattern Выходной паттерн
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_semantic_learn(const char *word,
                     const KolibriSemanticContext *ctx,
                     size_t generations,
                     KolibriSemanticPattern *pattern);

/**
 * Вычисление сходства между двумя паттернами
 * 
 * @param p1 Первый паттерн
 * @param p2 Второй паттерн
 * @return Сходство от 0.0 (разные) до 1.0 (идентичные)
 */
double k_semantic_similarity(const KolibriSemanticPattern *p1,
                            const KolibriSemanticPattern *p2);

/**
 * Поиск ближайшего паттерна из набора
 * 
 * @param pattern Целевой паттерн
 * @param candidates Набор кандидатов
 * @param count Количество кандидатов
 * @return Индекс ближайшего паттерна или -1
 */
int k_semantic_find_nearest(const KolibriSemanticPattern *pattern,
                            const KolibriSemanticPattern *candidates,
                            size_t count);

/**
 * Слияние двух паттернов (для распределённого обучения)
 * 
 * @param p1 Первый паттерн
 * @param p2 Второй паттерн
 * @param merged Результат слияния
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_semantic_merge_patterns(const KolibriSemanticPattern *p1,
                              const KolibriSemanticPattern *p2,
                              KolibriSemanticPattern *merged);

/**
 * Валидация паттерна в контексте
 * 
 * Проверяет, насколько хорошо паттерн соответствует контексту
 * 
 * @param pattern Паттерн для проверки
 * @param ctx Контекст
 * @return Оценка соответствия от 0.0 до 1.0
 */
double k_semantic_validate(const KolibriSemanticPattern *pattern,
                          const KolibriSemanticContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_SEMANTIC_H */
