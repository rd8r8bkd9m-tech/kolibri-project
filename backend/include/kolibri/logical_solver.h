/*
 * logical_solver.h
 *
 * Настоящий логический solver для Kolibri
 *
 * Архитектура:
 *   1. Парсер фактов — извлекает entities, constraints, properties из текста
 *   2. Constraint engine — propagates constraints, eliminates impossibilities
 *   3. Solver — находит единственное решение через elimination
 *
 * В отличие от шаблонов — РЕАЛЬНО решает задачу через логический вывод.
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_LOGICAL_SOLVER_H
#define KOLIBRI_LOGICAL_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ
 * ============================================================================ */

/** Максимальное количество entities */
#define KLS_MAX_ENTITIES 32

/** Максимальное количество свойств */
#define KLS_MAX_PROPERTIES 32

/** Максимальное количество ограничений */
#define KLS_MAX_CONSTRAINTS 64

/** Максимальное количество шагов вывода */
#define KLS_MAX_STEPS 32

/** Максимальная длина текста */
#define KLS_MAX_TEXT 256

/** Максимальное количество доменов (групп entities) */
#define KLS_MAX_DOMAINS 8

/** Максимальный размер домена */
#define KLS_MAX_DOMAIN_SIZE 16

/* ============================================================================
 * ТИПЫ ДАННЫХ
 * ============================================================================ */

/** Значение свойства */
typedef enum {
    KLS_VAL_UNKNOWN = -1,
    KLS_VAL_FALSE = 0,
    KLS_VAL_TRUE = 1,
    KLS_VAL_EXCLUDED = -2  /* Исключено */
} KolibriLSValue;

/** Тип ограничения */
typedef enum {
    KLS_CONSTRAINT_ONE_OF,      /* Entity имеет одно из значений */
    KLS_CONSTRAINT_NOT,          /* Entity НЕ имеет значение */
    KLS_CONSTRAINT_EQUALS,       /* Entity = значение */
    KLS_CONSTRAINT_IMPLIES,      /* Если A то B */
    KLS_CONSTRAINT_ALL_DIFFERENT /* Все entities разные */
} KolibriLSConstraintType;

/** Ограничение */
typedef struct {
    KolibriLSConstraintType type;
    int domain;                 /* Домен */
    int entity_idx;             /* Индекс entity */
    int prop_idx;               /* Индекс свойства */
    int value_idx;              /* Индекс значения */
    int entity2_idx;            /* Для implies */
    int value2_idx;             /* Для implies */
    int domain2;                /* Второй домен для implies */
    char reason[KLS_MAX_TEXT];  /* Почему это ограничение */
} KolibriLSConstraint;

/** Шаг вывода */
typedef struct {
    int step_num;
    char description[KLS_MAX_TEXT];
    char detail[KLS_MAX_TEXT];
    double confidence;
    int entities_changed;         /* Сколько entities изменились */
} KolibriLSStep;

/** Решение */
typedef struct {
    int solved;                   /* Решено? */
    int steps_count;
    KolibriLSStep steps[KLS_MAX_STEPS];
    char answer[KLS_MAX_TEXT * 4];
    double confidence;
    char reasoning_summary[KLS_MAX_TEXT * 2];
} KolibriLSSolution;

/** Логическая задача */
typedef struct {
    /* Домены */
    char domain_names[KLS_MAX_DOMAINS][KLS_MAX_TEXT];
    char domain_values[KLS_MAX_DOMAINS][KLS_MAX_DOMAIN_SIZE][KLS_MAX_TEXT];
    int domain_sizes[KLS_MAX_DOMAINS];
    int num_domains;

    /* Entity values: entity[domain][idx] = value_idx или KLS_VAL_UNKNOWN */
    int values[KLS_MAX_DOMAINS][KLS_MAX_DOMAIN_SIZE];
    
    /* Possibilities: possible[domain][entity_idx][value_idx] = 0/1 */
    int possible[KLS_MAX_DOMAINS][KLS_MAX_DOMAIN_SIZE][KLS_MAX_DOMAIN_SIZE];

    /* Constraints */
    KolibriLSConstraint constraints[KLS_MAX_CONSTRAINTS];
    int num_constraints;

    /* Решение */
    KolibriLSSolution solution;

    /* Timing */
    double solve_time_ms;
} KolibriLogicalSolver;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * Инициализировать solver
 */
int kolibri_ls_init(KolibriLogicalSolver *ls);

/**
 * Добавить домен с значениями
 *
 * @param ls         Solver
 * @param domain     Название домена ("выключатели", "лампочки")
 * @param values     Массив значений
 * @param num_values Количество значений
 * @return 0 на успех
 */
int kolibri_ls_add_domain(KolibriLogicalSolver *ls,
                         const char *domain,
                         const char **values,
                         int num_values);

/**
 * Добавить ограничение: entity НЕ имеет значение
 */
int kolibri_ls_add_not(KolibriLogicalSolver *ls,
                      int domain, int entity_idx, int value_idx,
                      const char *reason);

/**
 * Добавить ограничение: entity = значение
 */
int kolibri_ls_add_equals(KolibriLogicalSolver *ls,
                         int domain, int entity_idx, int value_idx,
                         const char *reason);

/**
 * Добавить ограничение: все entities в домене разные (bijection)
 */
int kolibri_ls_add_all_different(KolibriLogicalSolver *ls,
                                int domain,
                                const char *reason);

/**
 * Добавить ограничение: если entity1 = value1 то entity2 = value2
 */
int kolibri_ls_add_implies(KolibriLogicalSolver *ls,
                          int domain1, int entity1, int value1,
                          int domain2, int entity2, int value2,
                          const char *reason);

/**
 * Решить задачу через constraint propagation
 *
 * @param ls       Solver
 * @param solution Решение (output)
 * @return 0 если решено, 1 если неоднозначно, -1 если противоречие
 */
int kolibri_ls_solve(KolibriLogicalSolver *ls, KolibriLSSolution *solution);

/**
 * Распечатать решение
 */
void kolibri_ls_print_solution(const KolibriLSSolution *sol);

/**
 * Распечатать таблицу possibilities
 */
void kolibri_ls_print_grid(const KolibriLogicalSolver *ls);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_LOGICAL_SOLVER_H */
