/*
 * knowledge_base.h — Интерфейс фасада базы знаний
 *
 * Управляет семантическим индексом, динамическим добавлением знаний
 * и поиском (возвращает JSON).
 *
 * Copyright (c) 2025 Kolibri OS
 */

#ifndef KOLIBRI_FACADE_KNOWLEDGE_BASE_H
#define KOLIBRI_FACADE_KNOWLEDGE_BASE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Инициализация семантической базы знаний.
 * Загружает документы из указанной директории.
 * @param data_path Путь к директории с данными (например, "data/")
 * @return 0 при успехе
 */
int kolibri_kb_init(const char *data_path);

/**
 * Освобождение ресурсов базы знаний.
 */
void kolibri_kb_destroy(void);

/**
 * Семантический поиск в базе знаний.
 * @param query Поисковый запрос
 * @param out_json Буфер для JSON-ответа со списком релевантных документов
 * @param out_capacity Размер буфера
 * @return 0 при успехе
 */
int kolibri_kb_search(const char *query, char *out_json, size_t out_capacity);

/**
 * Динамическое добавление факта в базу знаний.
 * Добавляется в In-Memory индекс и в движок рассуждений (reasoning_engine.c).
 * @param fact Текст факта
 * @param confidence Степень уверенности (0.0 - 1.0)
 * @param source Источник факта
 * @return 0 при успехе
 */
int kolibri_kb_add_fact(const char *fact, double confidence, const char *source);

/**
 * Динамическое добавление правила в базу знаний.
 * @param premise Посылка
 * @param conclusion Заключение
 * @param op Логический оператор
 * @param strength Сила правила (0.0 - 1.0)
 * @param domain Домен (категория)
 * @return 0 при успехе
 */
int kolibri_kb_add_rule(const char *premise, const char *conclusion, int op, double strength, const char *domain);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_FACADE_KNOWLEDGE_BASE_H */
