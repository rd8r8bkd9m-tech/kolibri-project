/*
 * math_solver.h
 *
 * Математический решатель для Kolibri Numeric Transformer
 *
 * Возможности:
 *   - Линейные уравнения: ax + b = c
 *   - Квадратные уравнения: ax² + bx + c = 0
 *   - Системы линейных уравнений: 2x3
 *   - Пошаговые объяснения
 *   - Проверка ответов подстановкой
 *   - Интеграция с decimal.c для точных вычислений
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#ifndef KOLIBRI_MATH_SOLVER_H
#define KOLIBRI_MATH_SOLVER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * КОНСТАНТЫ И ТИПЫ
 * ============================================================================ */

/** Максимальное количество шагов в решении */
#define KMS_MAX_STEPS 64

/** Максимальная длина описания шага */
#define KMS_STEP_DESC_LEN 256

/** Максимальный размер системы уравнений */
#define KMS_MAX_VARS 10
#define KMS_MAX_EQS 10

/** Тип уравнения */
typedef enum {
    KMS_EQ_LINEAR = 0,      /* ax + b = c */
    KMS_EQ_QUADRATIC = 1,   /* ax² + bx + c = 0 */
    KMS_EQ_SYSTEM = 2,      /* Система линейных */
    KMS_EQ_UNKNOWN = -1
} KolibriEqType;

/** Тип решения */
typedef enum {
    KMS_SOL_ONE = 0,        /* Одно решение */
    KMS_SOL_TWO = 1,        /* Два решения (квадратное) */
    KMS_SOL_NONE = 2,       /* Нет решений */
    KMS_SOL_INFINITE = 3,   /* Бесконечно много */
    KMS_SOL_COMPLEX = 4,    /* Комплексные корни */
    KMS_SOL_ERROR = -1
} KolibriSolType;

/** Шаг решения */
typedef struct {
    int step_num;                      /* Номер шага */
    char description[KMS_STEP_DESC_LEN]; /* Описание шага */
    char expression[KMS_STEP_DESC_LEN];  /* Выражение на этом шаге */
    double result;                      /* Промежуточный результат */
} KolibriSolutionStep;

/** Решение одного уравнения */
typedef struct {
    KolibriEqType eq_type;
    KolibriSolType sol_type;

    /* Коэффициенты */
    double a, b, c;

    /* Решения */
    double x1, x2;          /* Для квадратного: два корня */
    double discriminant;    /* Для квадратного: D = b² - 4ac */
    int has_complex;        /* Есть комплексные корни? */

    /* Для систем */
    double solutions[KMS_MAX_VARS];
    int num_vars;

    /* Пошаговое решение */
    KolibriSolutionStep steps[KMS_MAX_STEPS];
    int num_steps;

    /* Мета-информация */
    char original_eq[KMS_STEP_DESC_LEN];
    char final_answer[KMS_STEP_DESC_LEN];
    double verification_error;  /* Ошибка после подстановки */
    int is_verified;        /* Ответ проверен? */
} KolibriEquationSolution;

/** Матрица для систем уравнений [eqs][vars+1] */
typedef struct {
    double matrix[KMS_MAX_EQS][KMS_MAX_VARS + 1];
    int num_eqs;
    int num_vars;
} KolibriMatrix;

/* ============================================================================
 * API: РЕШЕНИЕ УРАВНЕНИЙ
 * ============================================================================ */

/**
 * Решить линейное уравнение: ax + b = c
 *
 * @param a, b, c  Коэффициенты
 * @param solution Результат (output)
 * @return 0 на успех
 */
int kolibri_solve_linear(double a, double b, double c,
                          KolibriEquationSolution *solution);

/**
 * Решить квадратное уравнение: ax² + bx + c = 0
 *
 * @param a, b, c  Коэффициенты
 * @param solution Результат (output)
 * @return 0 на успех
 */
int kolibri_solve_quadratic(double a, double b, double c,
                            KolibriEquationSolution *solution);

/**
 * Решить систему линейных уравнений методом Гаусса
 *
 * @param matrix   Матрица системы [eqs][vars+1]
 * @param solution Результат (output)
 * @return 0 на успех
 */
int kolibri_solve_system(const KolibriMatrix *matrix,
                         KolibriEquationSolution *solution);

/**
 * Распознать тип уравнения из строки
 *
 * @param equation Строка с уравнением
 * @param a, b, c  Распознанные коэффициенты (output)
 * @return Тип уравнения
 */
KolibriEqType kolibri_parse_equation(const char *equation,
                                      double *a, double *b, double *c);

/**
 * Универсальный решатель: автоматически распознаёт и решает
 *
 * @param equation Строка с уравнением
 * @param solution Результат (output)
 * @return 0 на успех
 */
int kolibri_solve(const char *equation,
                  KolibriEquationSolution *solution);

/* ============================================================================
 * API: ПРОВЕРКА ОТВЕТОВ
 * ============================================================================ */

/**
 * Проверить решение подстановкой
 *
 * @param solution Решение для проверки
 * @return 0 если решение верно, иначе код ошибки
 */
int kolibri_verify_solution(KolibriEquationSolution *solution);

/**
 * Вычислить ошибку решения
 *
 * @param solution Решение
 * @return Максимальная абсолютная ошибка
 */
double kolibri_compute_error(const KolibriEquationSolution *solution);

/* ============================================================================
 * API: ФОРМАТИРОВАНИЕ
 * ============================================================================ */

/**
 * Форматировать решение в человекочитаемый вид
 *
 * @param solution Решение
 * @param output   Буфер для вывода
 * @param size     Размер буфера
 * @return Длина результата
 */
size_t kolibri_format_solution(const KolibriEquationSolution *solution,
                                char *output, size_t size);

/**
 * Форматировать пошаговое решение
 *
 * @param solution Решение
 * @param output   Буфер для вывода
 * @param size     Размер буфера
 * @return Длина результата
 */
size_t kolibri_format_steps(const KolibriEquationSolution *solution,
                            char *output, size_t size);

/**
 * Распечатать решение (для отладки)
 */
void kolibri_print_solution(const KolibriEquationSolution *solution);

/* ============================================================================
 * API: УТИЛИТЫ
 * ============================================================================ */

/**
 * Вычислить определитель матрицы
 */
double kolibri_determinant(const KolibriMatrix *matrix);

/**
 * Вычислить НОД двух чисел
 */
double kolibri_gcd(double a, double b);

/**
 * Вычислить НОК двух чисел
 */
double kolibri_lcm(double a, double b);

/**
 * Упростить дробь
 */
void kolibri_simplify_fraction(double numerator, double denominator,
                                double *out_num, double *out_den);

#ifdef __cplusplus
}
#endif

#endif /* KOLIBRI_MATH_SOLVER_H */
