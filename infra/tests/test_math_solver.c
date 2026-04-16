/*
 * test_math_solver.c
 *
 * Тесты для Symbolic Math Engine
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "kolibri/math_solver.h"

/* ============================================================================
 * ТЕСТЫ ЛИНЕЙНЫХ УРАВНЕНИЙ
 * ============================================================================ */

void test_linear_simple(void) {
    printf("Testing simple linear equation: 2x + 3 = 7\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_linear(2.0, 3.0, 7.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);
    assert(sol.eq_type == KMS_EQ_LINEAR);
    assert(sol.num_steps >= 3);

    /* Проверяем решение: x = 2 */
    assert(fabs(sol.x1 - 2.0) < 1e-6);

    /* Проверяем */
    int verified = kolibri_verify_solution(&sol);
    assert(verified == 0);
    assert(sol.is_verified);
    assert(sol.verification_error < 1e-6);

    /* Печатаем */
    kolibri_print_solution(&sol);

    printf("✓ Simple linear equation test passed\n\n");
}

void test_linear_no_constant(void) {
    printf("Testing linear without constant: 5x = 15\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_linear(5.0, 0.0, 15.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);
    assert(fabs(sol.x1 - 3.0) < 1e-6);

    kolibri_print_solution(&sol);
    printf("✓ Linear without constant test passed\n\n");
}

void test_linear_no_solution(void) {
    printf("Testing linear with no solution: 0x + 5 = 3\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_linear(0.0, 5.0, 3.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_NONE);

    kolibri_print_solution(&sol);
    printf("✓ Linear no solution test passed\n\n");
}

void test_linear_infinite_solutions(void) {
    printf("Testing linear with infinite solutions: 0x + 5 = 5\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_linear(0.0, 5.0, 5.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_INFINITE);

    kolibri_print_solution(&sol);
    printf("✓ Linear infinite solutions test passed\n\n");
}

void test_linear_negative(void) {
    printf("Testing linear with negative coefficients: -3x - 6 = -15\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_linear(-3.0, -6.0, -15.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);
    assert(fabs(sol.x1 - 3.0) < 1e-6);

    kolibri_print_solution(&sol);
    printf("✓ Linear negative coefficients test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ КВАДРАТНЫХ УРАВНЕНИЙ
 * ============================================================================ */

void test_quadratic_two_roots(void) {
    printf("Testing quadratic with two roots: x² - 5x + 6 = 0\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_quadratic(1.0, -5.0, 6.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_TWO);
    assert(sol.discriminant > 0);
    assert(!sol.has_complex);

    /* Корни: x = 2, x = 3 */
    assert(fabs(sol.x1 - 3.0) < 1e-6 || fabs(sol.x1 - 2.0) < 1e-6);
    assert(fabs(sol.x2 - 2.0) < 1e-6 || fabs(sol.x2 - 3.0) < 1e-6);

    /* Проверяем оба корня */
    kolibri_verify_solution(&sol);
    assert(sol.is_verified);

    kolibri_print_solution(&sol);

    /* Форматируем шаги */
    char steps[1024];
    kolibri_format_steps(&sol, steps, sizeof(steps));
    printf("Formatted steps:\n%s\n", steps);

    printf("✓ Quadratic two roots test passed\n\n");
}

void test_quadratic_one_root(void) {
    printf("Testing quadratic with one root: x² - 4x + 4 = 0\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_quadratic(1.0, -4.0, 4.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);
    assert(fabs(sol.discriminant) < 1e-6);

    /* Корень: x = 2 */
    assert(fabs(sol.x1 - 2.0) < 1e-6);

    kolibri_verify_solution(&sol);
    assert(sol.is_verified);

    kolibri_print_solution(&sol);
    printf("✓ Quadratic one root test passed\n\n");
}

void test_quadratic_complex_roots(void) {
    printf("Testing quadratic with complex roots: x² + 1 = 0\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_quadratic(1.0, 0.0, 1.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_COMPLEX);
    assert(sol.has_complex);
    assert(sol.discriminant < 0);

    /* Корни: x = ±i */
    assert(fabs(sol.x1) < 1e-6);  /* Real part = 0 */
    assert(fabs(sol.x2 - 1.0) < 1e-6);  /* Imaginary part = 1 */

    kolibri_print_solution(&sol);
    printf("✓ Quadratic complex roots test passed\n\n");
}

void test_quadratic_negative_discriminant(void) {
    printf("Testing quadratic with negative discriminant: x² + 2x + 5 = 0\n");

    KolibriEquationSolution sol;
    int ret = kolibri_solve_quadratic(1.0, 2.0, 5.0, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_COMPLEX);
    assert(sol.discriminant < 0);

    /* D = 4 - 20 = -16 */
    assert(fabs(sol.discriminant - (-16.0)) < 1e-6);

    kolibri_print_solution(&sol);
    printf("✓ Quadratic negative discriminant test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ СИСТЕМ УРАВНЕНИЙ
 * ============================================================================ */

void test_system_2x2(void) {
    printf("Testing 2x2 system:\n");
    printf("  2x + y = 5\n");
    printf("  x - y = 1\n");

    KolibriMatrix mat;
    mat.num_eqs = 2;
    mat.num_vars = 2;

    /* 2x + y = 5 */
    mat.matrix[0][0] = 2.0;
    mat.matrix[0][1] = 1.0;
    mat.matrix[0][2] = 5.0;

    /* x - y = 1 */
    mat.matrix[1][0] = 1.0;
    mat.matrix[1][1] = -1.0;
    mat.matrix[1][2] = 1.0;

    KolibriEquationSolution sol;
    int ret = kolibri_solve_system(&mat, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);
    assert(sol.num_vars == 2);

    /* Решение: x = 2, y = 1 */
    assert(fabs(sol.solutions[0] - 2.0) < 1e-6);
    assert(fabs(sol.solutions[1] - 1.0) < 1e-6);

    kolibri_print_solution(&sol);
    printf("✓ 2x2 system test passed\n\n");
}

void test_system_3x3(void) {
    printf("Testing 3x3 system:\n");
    printf("  x + y + z = 6\n");
    printf("  2x - y + z = 3\n");
    printf("  x + 2y - z = 2\n");

    KolibriMatrix mat;
    mat.num_eqs = 3;
    mat.num_vars = 3;

    /* x + y + z = 6 */
    mat.matrix[0][0] = 1.0; mat.matrix[0][1] = 1.0; mat.matrix[0][2] = 1.0; mat.matrix[0][3] = 6.0;
    /* 2x - y + z = 3 */
    mat.matrix[1][0] = 2.0; mat.matrix[1][1] = -1.0; mat.matrix[1][2] = 1.0; mat.matrix[1][3] = 3.0;
    /* x + 2y - z = 2 */
    mat.matrix[2][0] = 1.0; mat.matrix[2][1] = 2.0; mat.matrix[2][2] = -1.0; mat.matrix[2][3] = 2.0;

    KolibriEquationSolution sol;
    int ret = kolibri_solve_system(&mat, &sol);

    assert(ret == 0);
    assert(sol.sol_type == KMS_SOL_ONE);

    /* Решение: x = 1, y = 2, z = 3 */
    assert(fabs(sol.solutions[0] - 1.0) < 1e-6);
    assert(fabs(sol.solutions[1] - 2.0) < 1e-6);
    assert(fabs(sol.solutions[2] - 3.0) < 1e-6);

    kolibri_print_solution(&sol);
    printf("✓ 3x3 system test passed\n\n");
}

/* ============================================================================
 * ТЕСТЫ УТИЛИТ
 * ============================================================================ */

void test_gcd_lcm(void) {
    printf("Testing GCD and LCM...\n");

    /* GCD */
    assert(fabs(kolibri_gcd(12.0, 8.0) - 4.0) < 1e-6);
    assert(fabs(kolibri_gcd(54.0, 24.0) - 6.0) < 1e-6);
    assert(fabs(kolibri_gcd(100.0, 75.0) - 25.0) < 1e-6);

    /* LCM */
    assert(fabs(kolibri_lcm(4.0, 6.0) - 12.0) < 1e-6);
    assert(fabs(kolibri_lcm(3.0, 5.0) - 15.0) < 1e-6);

    printf("✓ GCD and LCM test passed\n\n");
}

void test_simplify_fraction(void) {
    printf("Testing fraction simplification...\n");

    double num, den;

    kolibri_simplify_fraction(6.0, 8.0, &num, &den);
    assert(fabs(num - 3.0) < 1e-6);
    assert(fabs(den - 4.0) < 1e-6);

    kolibri_simplify_fraction(-4.0, 6.0, &num, &den);
    assert(fabs(num - (-2.0)) < 1e-6);
    assert(fabs(den - 3.0) < 1e-6);

    kolibri_simplify_fraction(0.0, 5.0, &num, &den);
    assert(fabs(num) < 1e-6);
    assert(fabs(den - 1.0) < 1e-6);

    printf("✓ Fraction simplification test passed\n\n");
}

void test_formatting(void) {
    printf("Testing solution formatting...\n");

    KolibriEquationSolution sol;
    kolibri_solve_linear(3.0, 2.0, 11.0, &sol);

    /* Форматируем решение */
    char answer[256];
    size_t len = kolibri_format_solution(&sol, answer, sizeof(answer));
    assert(len > 0);
    assert(strstr(answer, "3") != NULL);  /* Должно содержать ответ */

    printf("Formatted answer: %s\n", answer);

    /* Форматируем шаги */
    char steps[1024];
    len = kolibri_format_steps(&sol, steps, sizeof(steps));
    assert(len > 0);
    assert(strstr(steps, "Шаг") != NULL);
    assert(strstr(steps, "Уравнение") != NULL);

    printf("✓ Solution formatting test passed\n\n");
}

/* ============================================================================
 * ИНТЕГРАЦИОННЫЙ ТЕСТ
 * ============================================================================ */

void test_integrated_workflow(void) {
    printf("Testing integrated math workflow...\n");

    /* 1. Решаем уравнение */
    KolibriEquationSolution sol;
    kolibri_solve_quadratic(2.0, -8.0, 6.0, &sol);

    printf("Уравнение: %s\n", sol.original_eq);
    printf("Решение: %s\n", sol.final_answer);

    /* 2. Проверяем */
    kolibri_verify_solution(&sol);
    assert(sol.is_verified);
    printf("Проверка: ✓ (ошибка: %.2e)\n", sol.verification_error);

    /* 3. Форматируем для вывода */
    char output[1024];
    kolibri_format_steps(&sol, output, sizeof(output));

    assert(strlen(output) > 50);  /* Должно быть достаточно длинным */
    printf("\nПолное решение:\n%s\n", output);

    printf("✓ Integrated workflow test passed\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Symbolic Math Engine Tests\n");
    printf("===========================================\n\n");

    /* Линейные уравнения */
    printf("--- Linear Equations ---\n\n");
    test_linear_simple();
    test_linear_no_constant();
    test_linear_no_solution();
    test_linear_infinite_solutions();
    test_linear_negative();

    /* Квадратные уравнения */
    printf("--- Quadratic Equations ---\n\n");
    test_quadratic_two_roots();
    test_quadratic_one_root();
    test_quadratic_complex_roots();
    test_quadratic_negative_discriminant();

    /* Системы уравнений */
    printf("--- Systems of Equations ---\n\n");
    test_system_2x2();
    test_system_3x3();

    /* Утилиты */
    printf("--- Utilities ---\n\n");
    test_gcd_lcm();
    test_simplify_fraction();
    test_formatting();

    /* Интеграционный тест */
    printf("--- Integrated Test ---\n\n");
    test_integrated_workflow();

    printf("===========================================\n");
    printf("All tests passed! ✓\n");
    printf("===========================================\n");

    return 0;
}
