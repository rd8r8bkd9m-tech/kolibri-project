/*
 * fact_extractor.h
 *
 * Парсер фактов из текста на русском языке для логических задач
 *
 * Архитектура:
 *   1. Классификация типа задачи (коробки, монеты, выключатели, Эйнштейн)
 *   2. Извлечение сущностей (числа, объекты, свойства)
 *   3. Генерация constraints для solver
 *   4. Для некоторых задач — генерация ключевых наблюдений
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_FACT_EXTRACTOR_H
#define KOLIBRI_FACT_EXTRACTOR_H

#include "kolibri/logical_solver.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество извлечённых фактов */
#define KFE_MAX_FACTS 64

/** Максимальная длина факта */
#define KFE_MAX_FACT_TEXT 256

/** Максимальное количество ключевых слов */
#define KFE_MAX_KEYWORDS 32

/** Максимальная длина ключевого слова */
#define KFE_MAX_KEYWORD_LEN 64

/* ============================================================================
 * ТИПЫ ЗАДАЧ
 * ============================================================================ */

typedef enum {
    KFE_TASK_UNKNOWN = 0,
    KFE_TASK_MISLABELED_BOXES,    /* Неправильные надписи на коробках */
    KFE_TASK_FAKE_COIN,           /* Фальшивая монета */
    KFE_TASK_LIGHT_SWITCHES,      /* Выключатели и лампочки */
    KFE_TASK_EINSTEIN,            /* Загадка Эйнштейна */
    KFE_TASK_ASSIGNMENT,          /* Назначение (кто чем занимается) */
    KFE_TASK_COUNT
} KolibriFETaskType;

/* ============================================================================
 * ИЗВЛЕЧЁННЫЕ ДАННЫЕ
 * ============================================================================ */

/** Извлечённое число */
typedef struct {
    int value;
    char context[KFE_MAX_FACT_TEXT];  /* Контекст (монет, коробок, и т.д.) */
    int position;                      /* Позиция в тексте */
} KFEExtractedNumber;

/** Извлечённый объект */
typedef struct {
    char name[KFE_MAX_FACT_TEXT];
    char type[KFE_MAX_FACT_TEXT];  /* коробка, монета, выключатель, лампочка */
    int index;                      /* Индекс (1-based) */
} KFEExtractedEntity;

/** Извлечённое свойство */
typedef struct {
    char name[KFE_MAX_FACT_TEXT];
    char value[KFE_MAX_FACT_TEXT];
    char entity_type[KFE_MAX_FACT_TEXT];
    int entity_index;
    int is_negative;  /* true если "не", "неправильная" */
} KFEExtractedProperty;

/** Извлечённые данные */
typedef struct {
    KolibriFETaskType task_type;
    char task_name[64];
    
    KFEExtractedNumber numbers[KFE_MAX_FACTS];
    int num_numbers;
    
    KFEExtractedEntity entities[KFE_MAX_FACTS];
    int num_entities;
    
    KFEExtractedEntity entity_values[KFE_MAX_FACTS];
    int num_entity_values;
    
    KFEExtractedProperty properties[KFE_MAX_FACTS];
    int num_properties;
    
    /* Ключевые наблюдения (для генерации constraints) */
    char key_observations[KFE_MAX_FACTS][KFE_MAX_FACT_TEXT];
    int num_observations;
    
    /* Сгенерированные constraints */
    int generated_constraints[KFE_MAX_FACTS];
    int num_generated_constraints;
    
    /* Решение (если найдено) */
    char solution_text[KFE_MAX_FACT_TEXT * 4];
    int solved;
} KolibriFEExtractedData;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Извлечь факты из текста задачи
 *
 * @param text      Текст задачи
 * @param data      Извлечённые данные (output)
 * @return 0 на успех
 */
int kolibri_fe_extract(const char *text, KolibriFEExtractedData *data);

/**
 * Сгенерировать constraints для solver на основе извлечённых фактов
 *
 * @param data   Извлечённые данные
 * @param ls     Solver
 * @return Количество сгенерированных constraints
 */
int kolibri_fe_generate_constraints(const KolibriFEExtractedData *data,
                                    KolibriLogicalSolver *ls);

/**
 * Решить задачу через solver
 *
 * @param data       Извлечённые данные
 * @param ls         Solver
 * @param solution   Решение (output)
 * @return 0 если решено
 */
int kolibri_fe_solve(const KolibriFEExtractedData *data,
                    KolibriLogicalSolver *ls,
                    KolibriLSSolution *solution);

/**
 * Получить название типа задачи
 */
const char* kolibri_fe_task_type_name(KolibriFETaskType type);

/**
 * Распечатать извлечённые факты
 */
void kolibri_fe_print_extracted(const KolibriFEExtractedData *data);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_FACT_EXTRACTOR_H */
