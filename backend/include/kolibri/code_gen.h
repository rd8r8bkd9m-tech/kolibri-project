/*
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 * 
 * Code Generation Module Header
 * Специализированная генерация программного кода
 */

#ifndef KOLIBRI_CODE_GEN_H
#define KOLIBRI_CODE_GEN_H

#include "kolibri/generation.h"
#include "kolibri/corpus.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Типы генерируемого кода */
typedef enum {
    KOLIBRI_CODE_C,           /* Язык C */
    KOLIBRI_CODE_PYTHON,      /* Python */
    KOLIBRI_CODE_KOLIBRI_SCRIPT, /* Kolibri Script (.ks) */
    KOLIBRI_CODE_JSON,        /* JSON структуры */
    KOLIBRI_CODE_FORMULA,     /* Формулы компрессии */
    KOLIBRI_CODE_AUTO         /* Авто-определение по контексту */
} KolibriCodeLanguage;

/* Статистика генерации кода */
typedef struct {
    size_t lines_generated;
    size_t functions_generated;
    size_t variables_generated;
    size_t control_structures;
    double avg_function_complexity;
} KolibriCodeGenStats;

/* Контекст генерации кода */
typedef struct {
    void *gen_ctx;              /* Базовый контекст генерации (непрозрачный) */
    int language;               /* KolibriCodeLanguage */
    int current_indent;         /* Текущий уровень отступа */
    char line_buffer[256];      /* Буфер для строки */
    KolibriCodeGenStats stats;  /* Статистика */
    void *template_cache;       /* Кэш шаблонов (непрозрачный) */
    size_t template_count;      /* Количество шаблонов */
} KolibriCodeGenContext;

/**
 * Инициализация контекста генерации кода
 * 
 * @param ctx Контекст генерации кода (должен быть выделен)
 * @param corpus Корпус с изученными паттернами
 * @param language Целевой язык программирования
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_init(KolibriCodeGenContext *ctx,
                   KolibriCorpusContext *corpus,
                   KolibriCodeLanguage language);

/**
 * Освобождение ресурсов контекста генерации кода
 * 
 * @param ctx Контекст генерации кода
 */
void k_codegen_free(KolibriCodeGenContext *ctx);

/**
 * Установка уровня отступа
 * 
 * @param ctx Контекст генерации кода
 * @param indent Уровень отступа (0-16)
 */
void k_codegen_set_indent(KolibriCodeGenContext *ctx, int indent);

/**
 * Увеличение уровня отступа на 1
 * 
 * @param ctx Контекст генерации кода
 */
void k_codegen_increase_indent(KolibriCodeGenContext *ctx);

/**
 * Уменьшение уровня отступа на 1
 * 
 * @param ctx Контекст генерации кода
 */
void k_codegen_decrease_indent(KolibriCodeGenContext *ctx);

/**
 * Генерация строки кода по имени шаблона
 * 
 * @param ctx Контекст генерации кода
 * @param template_name Имя шаблона (например, "function_def", "if_statement")
 * @param params Массив параметров для подстановки
 * @param param_count Количество параметров
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_line(KolibriCodeGenContext *ctx,
                           const char *template_name,
                           const char **params,
                           size_t param_count,
                           char *output,
                           size_t output_size);

/**
 * Генерация функции
 * 
 * @param ctx Контекст генерации кода
 * @param return_type Тип возвращаемого значения (NULL для void/Python)
 * @param func_name Имя функции
 * @param params Параметры функции (строка)
 * @param body Тело функции
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_function(KolibriCodeGenContext *ctx,
                               const char *return_type,
                               const char *func_name,
                               const char *params,
                               const char *body,
                               char *output,
                               size_t output_size);

/**
 * Генерация объявления переменной
 * 
 * @param ctx Контекст генерации кода
 * @param var_type Тип переменной (NULL для Python)
 * @param var_name Имя переменной
 * @param initial_value Начальное значение (может быть NULL)
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_variable(KolibriCodeGenContext *ctx,
                               const char *var_type,
                               const char *var_name,
                               const char *initial_value,
                               char *output,
                               size_t output_size);

/**
 * Генерация цикла (for/while)
 * 
 * @param ctx Контекст генерации кода
 * @param loop_type Тип цикла ("for" или "while")
 * @param init Инициализация (для for)
 * @param condition Условие продолжения
 * @param increment Шаг (для for)
 * @param body Тело цикла
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_loop(KolibriCodeGenContext *ctx,
                           const char *loop_type,
                           const char *init,
                           const char *condition,
                           const char *increment,
                           const char *body,
                           char *output,
                           size_t output_size);

/**
 * Генерация условного оператора (if/else)
 * 
 * @param ctx Контекст генерации кода
 * @param condition Условие
 * @param then_body Тело ветки then
 * @param else_body Тело ветки else (может быть NULL)
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_if(KolibriCodeGenContext *ctx,
                         const char *condition,
                         const char *then_body,
                         const char *else_body,
                         char *output,
                         size_t output_size);

/**
 * Генерация кода из текстового описания (промпта)
 * 
 * @param ctx Контекст генерации кода
 * @param prompt Текстовое описание того, что нужно сгенерировать
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_from_prompt(KolibriCodeGenContext *ctx,
                                  const char *prompt,
                                  char *output,
                                  size_t output_size);

/**
 * Генерация полного файла кода по описанию и требованиям
 * 
 * @param ctx Контекст генерации кода
 * @param description Общее описание программы
 * @param requirements Массив требований/функций для реализации
 * @param req_count Количество требований
 * @param output Буфер для результата
 * @param output_size Размер буфера
 * @return 0 в случае успеха, -1 при ошибке
 */
int k_codegen_generate_file(KolibriCodeGenContext *ctx,
                           const char *description,
                           const char **requirements,
                           size_t req_count,
                           char *output,
                           size_t output_size);

/**
 * Получение статистики генерации
 * 
 * @param ctx Контекст генерации кода
 * @param lines_generated Количество сгенерированных строк (выход)
 * @param functions_generated Количество функций (выход)
 * @param variables_generated Количество переменных (выход)
 * @param control_structures Количество управляющих структур (выход)
 */
void k_codegen_get_stats(const KolibriCodeGenContext *ctx,
                        size_t *lines_generated,
                        size_t *functions_generated,
                        size_t *variables_generated,
                        size_t *control_structures);

/**
 * Вывод статистики генерации в stdout
 * 
 * @param ctx Контекст генерации кода
 */
void k_codegen_print_stats(const KolibriCodeGenContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_CODE_GEN_H */
