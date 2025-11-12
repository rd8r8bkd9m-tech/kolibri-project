/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Context Window Module
 * Модуль контекстного окна для обработки последовательностей
 */

#ifndef KOLIBRI_CONTEXT_H
#define KOLIBRI_CONTEXT_H

#include "kolibri/digits.h"
#include "kolibri/semantic.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Размер контекстного окна (в токенах) */
#define KOLIBRI_CONTEXT_WINDOW_SIZE 2048

/* Максимальное количество паттернов в окне */
#define KOLIBRI_CONTEXT_MAX_PATTERNS 128

/**
 * Токен в контекстном окне
 */
typedef struct {
    kolibri_potok_cifr digits;        /* Числовое представление токена */
    KolibriSemanticPattern pattern;   /* Семантический паттерн */
    double attention_weight;          /* Вес внимания (0.0 - 1.0) */
    size_t position;                  /* Позиция в окне */
} KolibriContextToken;

/**
 * Контекстное окно
 */
typedef struct {
    KolibriContextToken tokens[KOLIBRI_CONTEXT_WINDOW_SIZE]; /* Токены */
    size_t token_count;                   /* Текущее количество токенов */
    size_t current_position;              /* Текущая позиция */
    double *attention_matrix;             /* Матрица внимания (token_count x token_count) */
    size_t attention_matrix_size;         /* Размер выделенной матрицы */
} KolibriContextWindow;

/**
 * Инициализация контекстного окна
 * 
 * @param ctx Контекстное окно
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_init(KolibriContextWindow *ctx);

/**
 * Освобождение ресурсов контекстного окна
 * 
 * @param ctx Контекстное окно
 */
void k_context_window_free(KolibriContextWindow *ctx);

/**
 * Добавление токена в контекстное окно
 * 
 * @param ctx Контекстное окно
 * @param text Текст токена
 * @param pattern Семантический паттерн токена (может быть NULL)
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_add_token(KolibriContextWindow *ctx,
                               const char *text,
                               const KolibriSemanticPattern *pattern);

/**
 * Вычисление весов внимания (attention) для всех токенов
 * Использует числовую корреляцию между паттернами
 * 
 * @param ctx Контекстное окно
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_compute_attention(KolibriContextWindow *ctx);

/**
 * Получение токена по позиции
 * 
 * @param ctx Контекстное окно
 * @param position Позиция токена
 * @return Указатель на токен или NULL при ошибке
 */
const KolibriContextToken *k_context_window_get_token(const KolibriContextWindow *ctx,
                                                     size_t position);

/**
 * Получение веса внимания между двумя токенами
 * 
 * @param ctx Контекстное окно
 * @param from_pos Позиция исходного токена
 * @param to_pos Позиция целевого токена
 * @return Вес внимания (0.0 - 1.0) или -1.0 при ошибке
 */
double k_context_window_get_attention(const KolibriContextWindow *ctx,
                                     size_t from_pos,
                                     size_t to_pos);

/**
 * Извлечение наиболее релевантных токенов на основе attention
 * 
 * @param ctx Контекстное окно
 * @param query_position Позиция запроса
 * @param top_k Количество топовых токенов
 * @param result Массив для результата (размер >= top_k)
 * @return Количество извлечённых токенов или -1 при ошибке
 */
int k_context_window_extract_relevant(const KolibriContextWindow *ctx,
                                      size_t query_position,
                                      size_t top_k,
                                      size_t *result);

/**
 * Сброс контекстного окна (очистка всех токенов)
 * 
 * @param ctx Контекстное окно
 */
void k_context_window_reset(KolibriContextWindow *ctx);

/**
 * Скользящее окно - сдвиг с сохранением последних N токенов
 * 
 * @param ctx Контекстное окно
 * @param keep_last Количество токенов для сохранения
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_slide(KolibriContextWindow *ctx, size_t keep_last);

/**
 * Сериализация контекстного окна в поток цифр
 * Для передачи через роевую сеть
 * 
 * @param ctx Контекстное окно
 * @param stream Поток для записи
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_serialize(const KolibriContextWindow *ctx,
                               kolibri_potok_cifr *stream);

/**
 * Десериализация контекстного окна из потока цифр
 * 
 * @param ctx Контекстное окно
 * @param stream Поток для чтения
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_context_window_deserialize(KolibriContextWindow *ctx,
                                 const kolibri_potok_cifr *stream);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CONTEXT_H */
