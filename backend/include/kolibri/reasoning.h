/*
 * reasoning.h — Фасад движка рассуждений Kolibri
 *
 * Предоставляет высокоуровневый API для интеграции с внешними системами
 * (WASM, Python API). Управляет глобальной конфигурацией и форматированием.
 *
 * Copyright (c) 2025 Kolibri OS
 */

#ifndef KOLIBRI_FACADE_REASONING_H
#define KOLIBRI_FACADE_REASONING_H

#include <stddef.h>
#include "kolibri/reasoning_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Инициализация системы рассуждений с параметрами по умолчанию.
 */
int kolibri_reasoning_init(void);

/**
 * Получить указатель на глобальную конфигурацию рассуждений.
 * Позволяет изменять настройки извне.
 */
KolibriREConfig* kolibri_reasoning_get_config(void);

/**
 * Выполнение рассуждения по текстовому запросу.
 * @param query Текстовый запрос
 * @param out_answer Буфер для ответа
 * @param out_capacity Размер буфера
 * @return 0 при успехе
 */
int kolibri_reasoning_query(const char *query, char *out_answer, size_t out_capacity);

/**
 * Выполнение рассуждения и возврат результата в формате JSON.
 * Включает Chain of Thought (шаги вывода).
 * @param query Текстовый запрос
 * @param out_json Буфер для JSON-ответа
 * @param out_capacity Размер буфера
 * @return 0 при успехе
 */
int kolibri_reasoning_query_json(const char *query, char *out_json, size_t out_capacity);

/**
 * Выполнение "what if" (counterfactual) рассуждения.
 * @param query Текстовый запрос
 * @param what_if Предположение (что если)
 * @param out_answer Буфер для ответа
 * @param out_capacity Размер буфера
 * @return 0 при успехе
 */
int kolibri_reasoning_counterfactual(const char *query, const char *what_if, char *out_answer, size_t out_capacity);

/**
 * Выполнение абдуктивного рассуждения (поиск причин).
 * @param query Текстовый запрос
 * @param out_answer Буфер для ответа
 * @param out_capacity Размер буфера
 * @return 0 при успехе
 */
int kolibri_reasoning_abductive(const char *query, char *out_answer, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_FACADE_REASONING_H */
