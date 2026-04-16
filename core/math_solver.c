/*
 * math_solver.c
 *
 * Реализация математического решателя для Kolibri
 *
 * Поддерживает:
 *   - Линейные уравнения с пошаговым решением
 *   - Квадратные уравнения с дискриминантом
 *   - Системы линейных уравнений (метод Гаусса)
 *   - Проверку ответов подстановкой
 *   - Форматирование решений
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/math_solver.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * ВНУТРЕННИЕ ФУНКЦИИ
 * ============================================================================ */

/** Добавить шаг решения */
static void add_step(KolibriEquationSolution *sol, int num,
                     const char *desc, const char *expr, double result) {
    if (sol->num_steps >= KMS_MAX_STEPS) return;

    KolibriSolutionStep *step = &sol->steps[sol->num_steps];
    step->step_num = num;
    snprintf(step->description, KMS_STEP_DESC_LEN, "%s", desc);
    snprintf(step->expression, KMS_STEP_DESC_LEN, "%s", expr);
    step->result = result;
    sol->num_steps++;
}

/** Проверка на ноль с epsilon */
static int is_zero(double x) {
    return fabs(x) < 1e-10;
}

/* ============================================================================
 * ЛИНЕЙНЫЕ УРАВНЕНИЯ: ax + b = c
 * ============================================================================ */

int kolibri_solve_linear(double a, double b, double c,
                          KolibriEquationSolution *solution) {
    if (!solution) return -1;

    memset(solution, 0, sizeof(KolibriEquationSolution));
    solution->eq_type = KMS_EQ_LINEAR;
    solution->a = a;
    solution->b = b;
    solution->c = c;

    /* Форматируем исходное уравнение */
    if (is_zero(b)) {
        snprintf(solution->original_eq, KMS_STEP_DESC_LEN,
                 "%.2fx = %.2f", a, c);
    } else {
        snprintf(solution->original_eq, KMS_STEP_DESC_LEN,
                 "%.2fx + %.2f = %.2f", a, b, c);
    }

    /* Шаг 1: Записываем уравнение */
    add_step(solution, 1, "Записываем уравнение",
             solution->original_eq, 0);

    /* Случай: a = 0 */
    if (is_zero(a)) {
        if (is_zero(c - b)) {
            /* 0 = 0 — бесконечно много решений */
            solution->sol_type = KMS_SOL_INFINITE;
            snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                     "x — любое число (бесконечно много решений)");
            add_step(solution, 2, "Коэффициент при x = 0, уравнение тождественно",
                     "0 = 0", 0);
        } else {
            /* 0 = non-zero — нет решений */
            solution->sol_type = KMS_SOL_NONE;
            snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                     "Нет решений (противоречие: %.2f ≠ %.2f)", b, c);
            add_step(solution, 2, "Противоречие",
                     "Нет решений", 0);
        }
        return 0;
    }

    /* Шаг 2: Переносим b в правую часть */
    double right = c - b;
    char expr[128];
    snprintf(expr, sizeof(expr), "%.2fx = %.2f - %.2f = %.2f", a, c, b, right);
    add_step(solution, 2, "Переносим свободный член в правую часть",
             expr, right);

    /* Шаг 3: Делим на a */
    double x = right / a;
    snprintf(expr, sizeof(expr), "x = %.2f / %.2f = %.6f", right, a, x);
    add_step(solution, 3, "Делим обе части на коэффициент при x",
             expr, x);

    solution->sol_type = KMS_SOL_ONE;
    solution->x1 = x;
    solution->num_vars = 1;
    solution->solutions[0] = x;

    /* Форматируем ответ */
    snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
             "x = %.6f", x);

    return 0;
}

/* ============================================================================
 * КВАДРАТНЫЕ УРАВНЕНИЯ: ax² + bx + c = 0
 * ============================================================================ */

int kolibri_solve_quadratic(double a, double b, double c,
                            KolibriEquationSolution *solution) {
    if (!solution) return -1;

    memset(solution, 0, sizeof(KolibriEquationSolution));
    solution->eq_type = KMS_EQ_QUADRATIC;
    solution->a = a;
    solution->b = b;
    solution->c = c;

    /* Форматируем исходное уравнение */
    snprintf(solution->original_eq, KMS_STEP_DESC_LEN,
             "%.2fx² + %.2fx + %.2f = 0", a, b, c);

    /* Шаг 1: Записываем коэффициенты */
    char expr[128];
    snprintf(expr, sizeof(expr), "a = %.2f, b = %.2f, c = %.2f", a, b, c);
    add_step(solution, 1, "Определяем коэффициенты", expr, 0);

    /* Если a = 0, это линейное уравнение */
    if (is_zero(a)) {
        return kolibri_solve_linear(0, b, c, solution);
    }

    /* Шаг 2: Вычисляем дискриминант */
    double D = b * b - 4 * a * c;
    solution->discriminant = D;
    snprintf(expr, sizeof(expr),
             "D = b² - 4ac = (%.2f)² - 4(%.2f)(%.2f) = %.2f", b, a, c, D);
    add_step(solution, 2, "Вычисляем дискриминант", expr, D);

    /* Шаг 3: Анализируем дискриминант */
    if (D > 1e-10) {
        /* Два действительных корня */
        solution->sol_type = KMS_SOL_TWO;
        solution->has_complex = 0;

        add_step(solution, 3, "D > 0 — два действительных корня",
                 "Используем формулу: x = (-b ± √D) / 2a", D);

        double sqrt_D = sqrt(D);
        solution->x1 = (-b + sqrt_D) / (2 * a);
        solution->x2 = (-b - sqrt_D) / (2 * a);
        solution->num_vars = 2;
        solution->solutions[0] = solution->x1;
        solution->solutions[1] = solution->x2;

        snprintf(expr, sizeof(expr),
                 "x₁ = (%.2f + %.6f) / %.2f = %.6f",
                 -b, sqrt_D, 2 * a, solution->x1);
        add_step(solution, 4, "Вычисляем первый корень", expr, solution->x1);

        snprintf(expr, sizeof(expr),
                 "x₂ = (%.2f - %.6f) / %.2f = %.6f",
                 -b, sqrt_D, 2 * a, solution->x2);
        add_step(solution, 5, "Вычисляем второй корень", expr, solution->x2);

        snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                 "x₁ = %.6f, x₂ = %.6f", solution->x1, solution->x2);

    } else if (D > -1e-10) {
        /* Один корень (D = 0) */
        solution->sol_type = KMS_SOL_ONE;
        solution->has_complex = 0;

        add_step(solution, 3, "D = 0 — один корень (кратность 2)",
                 "x = -b / 2a", 0);

        solution->x1 = -b / (2 * a);
        solution->x2 = solution->x1;
        solution->num_vars = 1;
        solution->solutions[0] = solution->x1;

        snprintf(expr, sizeof(expr), "x = %.6f", solution->x1);
        add_step(solution, 4, "Вычисляем корень", expr, solution->x1);

        snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                 "x = %.6f (кратность 2)", solution->x1);

    } else {
        /* Комплексные корни (D < 0) */
        solution->sol_type = KMS_SOL_COMPLEX;
        solution->has_complex = 1;

        double sqrt_abs_D = sqrt(-D);
        double real_part = -b / (2 * a);
        double imag_part = sqrt_abs_D / (2 * a);

        snprintf(expr, sizeof(expr),
                 "D < 0 — комплексные корни\n"
                 "Re = -b/2a = %.6f, Im = √|D|/2a = %.6f",
                 real_part, imag_part);
        add_step(solution, 3, "D < 0 — комплексные корни", expr, D);

        solution->x1 = real_part;  /* Real part */
        solution->x2 = imag_part;  /* Imaginary part */

        snprintf(expr, sizeof(expr),
                 "x₁ = %.6f + %.6fi", real_part, imag_part);
        add_step(solution, 4, "Первый комплексный корень", expr, 0);

        snprintf(expr, sizeof(expr),
                 "x₂ = %.6f - %.6fi", real_part, imag_part);
        add_step(solution, 5, "Второй комплексный корень", expr, 0);

        snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                 "x₁ = %.6f + %.6fi, x₂ = %.6f - %.6fi",
                 real_part, imag_part, real_part, imag_part);
    }

    return 0;
}

/* ============================================================================
 * СИСТЕМЫ ЛИНЕЙНЫХ УРАВНЕНИЙ (метод Гаусса)
 * ============================================================================ */

int kolibri_solve_system(const KolibriMatrix *matrix,
                         KolibriEquationSolution *solution) {
    if (!matrix || !solution) return -1;

    memset(solution, 0, sizeof(KolibriEquationSolution));
    solution->eq_type = KMS_EQ_SYSTEM;
    solution->num_vars = matrix->num_vars;

    int n = matrix->num_eqs;
    int m = matrix->num_vars;

    /* Копируем матрицу */
    double mat[KMS_MAX_EQS][KMS_MAX_VARS + 1];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= m; j++) {
            mat[i][j] = matrix->matrix[i][j];
        }
    }

    /* Шаг 1: Записываем систему */
    add_step(solution, 1, "Записываем систему уравнений",
             "Метод Гаусса (исключение переменных)", 0);

    /* Прямой ход */
    int pivot_row = 0;
    for (int col = 0; col < m && pivot_row < n; col++) {
        /* Ищем ненулевой элемент в столбце */
        int sel = -1;
        for (int row = pivot_row; row < n; row++) {
            if (!is_zero(mat[row][col])) {
                sel = row;
                break;
            }
        }

        if (sel == -1) continue;  /* Столбец нулевой — свободная переменная */

        /* Меняем строки местами */
        if (sel != pivot_row) {
            for (int j = 0; j <= m; j++) {
                double tmp = mat[pivot_row][j];
                mat[pivot_row][j] = mat[sel][j];
                mat[sel][j] = tmp;
            }
        }

        /* Нормализуем строку */
        double pivot = mat[pivot_row][col];
        for (int j = col; j <= m; j++) {
            mat[pivot_row][j] /= pivot;
        }

        /* Исключаем переменную из других строк */
        for (int row = 0; row < n; row++) {
            if (row == pivot_row) continue;
            double factor = mat[row][col];
            for (int j = col; j <= m; j++) {
                mat[row][j] -= factor * mat[pivot_row][j];
            }
        }

        pivot_row++;
    }

    /* Шаг 2: Извлекаем решения */
    add_step(solution, 2, "Приводим к ступенчатому виду",
             "Обратный ход — извлекаем решения", 0);

    /* Проверяем на несовместность */
    for (int i = pivot_row; i < n; i++) {
        if (!is_zero(mat[i][m])) {
            solution->sol_type = KMS_SOL_NONE;
            snprintf(solution->final_answer, KMS_STEP_DESC_LEN,
                     "Система несовместна (нет решений)");
            add_step(solution, 3, "Обнаружено противоречие",
                     "0 = non-zero", 0);
            return 0;
        }
    }

    /* Извлекаем решения из приведённой матрицы */
    for (int i = 0; i < m; i++) {
        solution->solutions[i] = 0;
    }

    for (int i = 0; i < pivot_row && i < m; i++) {
        /* Ищем ведущий элемент */
        int lead_col = -1;
        for (int j = 0; j < m; j++) {
            if (!is_zero(mat[i][j])) {
                lead_col = j;
                break;
            }
        }
        if (lead_col >= 0) {
            solution->solutions[lead_col] = mat[i][m];
        }
    }

    /* Шаг 3: Форматируем решения */
    char answer[512] = "";
    int pos = 0;
    for (int i = 0; i < m; i++) {
        int len = snprintf(answer + pos, sizeof(answer) - pos,
                          "x%d = %.6f", i + 1, solution->solutions[i]);
        pos += len;
        if (i < m - 1) {
            len = snprintf(answer + pos, sizeof(answer) - pos, ", ");
            pos += len;
        }
    }

    solution->sol_type = KMS_SOL_ONE;
    snprintf(solution->final_answer, KMS_STEP_DESC_LEN, "%s", answer);

    add_step(solution, 3, "Решения системы", answer, 0);

    return 0;
}

/* ============================================================================
 * РАСПОЗНАВАНИЕ УРАВНЕНИЙ
 * ============================================================================ */

KolibriEqType kolibri_parse_equation(const char *equation, double *a_out, double *b_out, double *c_out) {
    if (!equation || !a_out || !b_out || !c_out)
        return KMS_EQ_UNKNOWN;

    *a_out = 0.0;
    *b_out = 0.0;
    *c_out = 0.0;

    /* Ищем '=' */
    const char *eq_sign = strchr(equation, '=');
    if (!eq_sign)
        return KMS_EQ_UNKNOWN;

    /* Ищем 'x' */
    const char *x_pos = strchr(equation, 'x');
    if (!x_pos)
        x_pos = strchr(equation, 'X');
    if (!x_pos)
        return KMS_EQ_UNKNOWN;

    /* Проверяем наличие квадрата */
    int has_square = (strstr(equation, "x^2") != NULL || strstr(equation, "x²") != NULL);

    if (has_square) {
        /* Упрощённый парсер квадратного уравнения */
        char a_buf[64] = {0}, b_buf[64] = {0}, c_buf[64] = {0}, d_buf[64] = {0};

        /* Ищем x^2 */
        const char *x2 = strstr(equation, "x^2");
        if (!x2)
            x2 = strstr(equation, "x²");

        /* Коэффициент 'a' (перед x^2) */
        const char *p = x2 - 1;
        while (p >= equation && (isdigit(*p) || *p == '.' || *p == '+' || *p == '-' || *p == ' '))
            p--;
        p++;
        size_t a_len = (size_t)(x2 - p);
        if (a_len > 0 && a_len < 63) {
            memcpy(a_buf, p, a_len);
            a_buf[a_len] = '\0';
        }

        /* Коэффициент 'b' (между x^2 и x) */
        const char *x1 = x_pos;
        if (x1 == x2) {
            /* Первое x — это часть x^2, ищем второе x */
            x1 = strchr(x2 + 1, 'x');
            if (!x1)
                x1 = strchr(x2 + 1, 'X');
        }

        if (x1) {
            const char *start_b = x2 + (strstr(x2, "x^2") == x2 ? 3 : 2);
            size_t b_len = (size_t)(x1 - start_b);
            if (b_len > 0 && b_len < 63) {
                memcpy(b_buf, start_b, b_len);
                b_buf[b_len] = '\0';
            }
        }

        /* Константа 'c' (между x и =) */
        const char *start_c = x1 ? x1 + 1 : x2 + (strstr(x2, "x^2") == x2 ? 3 : 2);
        size_t c_len = (size_t)(eq_sign - start_c);
        if (c_len > 0 && c_len < 63) {
            memcpy(c_buf, start_c, c_len);
            c_buf[c_len] = '\0';
        }

        /* Правая часть 'd' (после =) */
        const char *rhs = eq_sign + 1;
        size_t d_len = strlen(rhs);
        if (d_len > 0 && d_len < 63) {
            memcpy(d_buf, rhs, d_len);
            d_buf[d_len] = '\0';
        }

        /* Преобразуем */
        if (a_buf[0] == '\0' || strcmp(a_buf, "+") == 0 || strcmp(a_buf, " ") == 0)
            *a_out = 1.0;
        else if (strcmp(a_buf, "-") == 0)
            *a_out = -1.0;
        else
            *a_out = atof(a_buf);

        if (b_buf[0]) {
            if (strcmp(b_buf, "+") == 0 || strcmp(b_buf, " + ") == 0)
                *b_out = 1.0;
            else if (strcmp(b_buf, "-") == 0 || strcmp(b_buf, " - ") == 0)
                *b_out = -1.0;
            else
                *b_out = atof(b_buf);
        }

        if (c_buf[0])
            *c_out = atof(c_buf);

        double d_val = 0.0;
        if (d_buf[0])
            d_val = atof(d_buf);

        /* Приводим к виду ax^2 + bx + (c - d) = 0 */
        *c_out -= d_val;

        return KMS_EQ_QUADRATIC;

    } else {
        /* Линейное: ax + b = c */
        char a_buf[64] = {0}, b_buf[64] = {0}, c_buf[64] = {0};

        /* Коэффициент 'a' */
        const char *p = x_pos - 1;
        while (p >= equation && (isdigit(*p) || *p == '.' || *p == '+' || *p == '-' || *p == ' '))
            p--;
        p++;
        size_t a_len = (size_t)(x_pos - p);
        if (a_len > 0 && a_len < 63) {
            memcpy(a_buf, p, a_len);
            a_buf[a_len] = '\0';
        }

        /* Константа 'b' (между x и =) */
        size_t b_len = (size_t)(eq_sign - (x_pos + 1));
        if (b_len > 0 && b_len < 63) {
            memcpy(b_buf, x_pos + 1, b_len);
            b_buf[b_len] = '\0';
        }

        /* Правая часть 'c' */
        const char *rhs = eq_sign + 1;
        size_t c_len = strlen(rhs);
        if (c_len > 0 && c_len < 63) {
            memcpy(c_buf, rhs, c_len);
            c_buf[c_len] = '\0';
        }

        /* Преобразуем */
        if (a_buf[0] == '\0' || strcmp(a_buf, "+") == 0 || strcmp(a_buf, " ") == 0)
            *a_out = 1.0;
        else if (strcmp(a_buf, "-") == 0)
            *a_out = -1.0;
        else
            *a_out = atof(a_buf);

        if (b_buf[0])
            *b_out = atof(b_buf);
        if (c_buf[0])
            *c_out = atof(c_buf);

        return KMS_EQ_LINEAR;
    }
}

int kolibri_solve(const char *equation,
                  KolibriEquationSolution *solution) {
    if (!equation || !solution) return -1;

    double a, b, c;
    KolibriEqType type = kolibri_parse_equation(equation, &a, &b, &c);

    switch (type) {
        case KMS_EQ_LINEAR:
            return kolibri_solve_linear(a, b, c, solution);
        case KMS_EQ_QUADRATIC:
            return kolibri_solve_quadratic(a, b, c, solution);
        default:
            return -1;
    }
}

/* ============================================================================
 * ПРОВЕРКА РЕШЕНИЙ
 * ============================================================================ */

int kolibri_verify_solution(KolibriEquationSolution *solution) {
    if (!solution) return -1;

    double error = kolibri_compute_error(solution);
    solution->verification_error = error;
    solution->is_verified = (error < 1e-6);

    return solution->is_verified ? 0 : -1;
}

double kolibri_compute_error(const KolibriEquationSolution *solution) {
    if (!solution) return -1.0;

    double max_error = 0.0;

    if (solution->eq_type == KMS_EQ_LINEAR) {
        /* Проверяем: a*x + b = c */
        double x = solution->x1;
        double lhs = solution->a * x + solution->b;
        double rhs = solution->c;
        max_error = fabs(lhs - rhs);

    } else if (solution->eq_type == KMS_EQ_QUADRATIC) {
        /* Проверяем: ax² + bx + c = 0 для каждого корня */
        double errors[2];
        int count = (solution->sol_type == KMS_SOL_TWO) ? 2 : 1;

        for (int i = 0; i < count; i++) {
            double x = (i == 0) ? solution->x1 : solution->x2;
            double val = solution->a * x * x + solution->b * x + solution->c;
            errors[i] = fabs(val);
            if (errors[i] > max_error) max_error = errors[i];
        }
    }

    return max_error;
}

/* ============================================================================
 * ФОРМАТИРОВАНИЕ
 * ============================================================================ */

size_t kolibri_format_solution(const KolibriEquationSolution *solution,
                                char *output, size_t size) {
    if (!solution || !output || size == 0) return 0;

    return snprintf(output, size, "%s", solution->final_answer);
}

size_t kolibri_format_steps(const KolibriEquationSolution *solution,
                            char *output, size_t size) {
    if (!solution || !output || size == 0) return 0;

    size_t pos = 0;
    pos += snprintf(output + pos, size - pos,
                    "Уравнение: %s\n\n", solution->original_eq);

    for (int i = 0; i < solution->num_steps && pos < size - 1; i++) {
        const KolibriSolutionStep *step = &solution->steps[i];
        pos += snprintf(output + pos, size - pos,
                       "Шаг %d: %s\n", step->step_num, step->description);
        if (step->expression[0]) {
            pos += snprintf(output + pos, size - pos,
                           "  %s\n", step->expression);
        }
        pos += snprintf(output + pos, size - pos, "\n");
    }

    pos += snprintf(output + pos, size - pos,
                    "Ответ: %s\n", solution->final_answer);

    return pos;
}

void kolibri_print_solution(const KolibriEquationSolution *solution) {
    if (!solution) return;

    printf("========================================\n");
    printf("Уравнение: %s\n", solution->original_eq);
    printf("Тип: ");

    switch (solution->eq_type) {
        case KMS_EQ_LINEAR:    printf("Линейное\n"); break;
        case KMS_EQ_QUADRATIC: printf("Квадратное\n"); break;
        case KMS_EQ_SYSTEM:    printf("Система\n"); break;
        default:               printf("Неизвестно\n"); break;
    }

    printf("\n--- Пошаговое решение ---\n");
    for (int i = 0; i < solution->num_steps; i++) {
        const KolibriSolutionStep *step = &solution->steps[i];
        printf("Шаг %d: %s\n", step->step_num, step->description);
        if (step->expression[0]) {
            printf("  %s\n", step->expression);
        }
        printf("\n");
    }

    printf("Ответ: %s\n", solution->final_answer);

    if (solution->is_verified) {
        printf("Проверка: ✓ Верно (ошибка: %.2e)\n",
               solution->verification_error);
    } else {
        printf("Проверка: ✗ Не проверена\n");
    }

    printf("========================================\n\n");
}

/* ============================================================================
 * УТИЛИТЫ
 * ============================================================================ */

double kolibri_determinant(const KolibriMatrix *matrix) {
    if (!matrix || matrix->num_vars != matrix->num_eqs) return 0.0;

    int n = matrix->num_vars;
    if (n > KMS_MAX_VARS) return 0.0;

    /* Копируем матрицу */
    double mat[KMS_MAX_VARS][KMS_MAX_VARS];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            mat[i][j] = matrix->matrix[i][j];
        }
    }

    /* Метод Гаусса для определителя */
    double det = 1.0;
    for (int col = 0; col < n; col++) {
        /* Ищем ведущий элемент */
        int pivot = col;
        for (int row = col + 1; row < n; row++) {
            if (fabs(mat[row][col]) > fabs(mat[pivot][col])) {
                pivot = row;
            }
        }

        /* Меняем строки */
        if (pivot != col) {
            for (int j = 0; j < n; j++) {
                double tmp = mat[col][j];
                mat[col][j] = mat[pivot][j];
                mat[pivot][j] = tmp;
            }
            det = -det;  /* Знак меняется при交换е строк */
        }

        if (is_zero(mat[col][col])) return 0.0;

        det *= mat[col][col];

        /* Исключаем */
        for (int row = col + 1; row < n; row++) {
            double factor = mat[row][col] / mat[col][col];
            for (int j = col; j < n; j++) {
                mat[row][j] -= factor * mat[col][j];
            }
        }
    }

    return det;
}

double kolibri_gcd(double a, double b) {
    a = fabs(a);
    b = fabs(b);

    while (b > 1e-10) {
        double temp = b;
        b = fmod(a, b);
        a = temp;
    }

    return a;
}

double kolibri_lcm(double a, double b) {
    if (is_zero(a) || is_zero(b)) return 0.0;
    return fabs(a * b) / kolibri_gcd(a, b);
}

void kolibri_simplify_fraction(double numerator, double denominator,
                                double *out_num, double *out_den) {
    if (is_zero(denominator)) {
        *out_num = 0;
        *out_den = 1;
        return;
    }

    if (is_zero(numerator)) {
        *out_num = 0;
        *out_den = 1;
        return;
    }

    double common = kolibri_gcd(fabs(numerator), fabs(denominator));
    *out_num = numerator / common;
    *out_den = denominator / common;

    /* Знаменатель всегда положительный */
    if (*out_den < 0) {
        *out_num = -*out_num;
        *out_den = -*out_den;
    }
}
