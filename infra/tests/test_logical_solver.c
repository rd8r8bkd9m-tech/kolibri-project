/*
 * test_logical_solver.c
 *
 * Тесты для настоящего логического solver
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/logical_solver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================================
 * ТЕСТ: 3 выключателя, 3 лампочки
 * ============================================================================ */

void test_light_switches(void) {
    printf("=== Тест: 3 выключателя, 3 лампочки ===\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    /* Домены */
    const char *switches[] = {"Вкл 1", "Вкл 2", "Вкл 3"};
    const char *bulbs[] = {"Лампа A", "Лампа B", "Лампа C"};
    
    /* Создаём домен mapping: switch → bulb */
    /* Используем один домен size=3, entity i = switch i, value j = bulb j */
    kolibri_ls_add_domain(&ls, "switch→bulb", bulbs, 3);

    /* Ограничения из условия:
     * "Включи Вкл 1 на 5 мин, выключи, включи Вкл 2"
     * "Горит → Вкл 2", "Тёплая → Вкл 1", "Холодная → Вкл 3"
     * 
     * Но это не ограничения — это СТРАТЕГИЯ решения.
     * 
     * Настоящие ограничения: each switch maps to exactly one bulb (bijection)
     */
    kolibri_ls_add_all_different(&ls, 0, "Каждый выключатель → своя лампа");

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("Результат: %s\n", ret == 0 ? "РЕШЕНО" : (ret == 1 ? "ЧАСТИЧНО" : "ПРОТИВОРЕЧИЕ"));
    printf("\nОтвет:\n%s\n", sol.answer);
    
    kolibri_ls_print_solution(&sol);

    printf("✓ Тест выключателей завершён\n\n");
}

/* ============================================================================
 * ТЕСТ: 3 коробки с неправильными надписями
 * ============================================================================ */

void test_mislabeled_boxes(void) {
    printf("=== Тест: 3 коробки с неправильными надписями ===\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    /* Домен: коробка → содержимое */
    const char *contents[] = {"Яблоки", "Груши", "Яблоки+Груши"};
    kolibri_ls_add_domain(&ls, "коробка→содержимое", contents, 3);

    /* Надписи неправильные:
     * Коробка 1 (надпись "Яблоки") ≠ Яблоки
     * Коробка 2 (надпись "Груши") ≠ Груши
     * Коробка 3 (надпись "Я+Г") ≠ Я+Г
     */
    kolibri_ls_add_not(&ls, 0, 0, 0, "Надпись 'Яблоки' неправильная");
    kolibri_ls_add_not(&ls, 0, 1, 1, "Надпись 'Груши' неправильная");
    kolibri_ls_add_not(&ls, 0, 2, 2, "Надпись 'Я+Г' неправильная");

    /* All different */
    kolibri_ls_add_all_different(&ls, 0, "Все коробки разные");

    /* КЛЮЧЕВОЙ ШАГ: достаём фрукт из коробки 3 (надпись "Я+Г")
     * Если достали яблоко → коробка 3 = Яблоки (не Я+Г, значит только яблоки)
     * Это разблокирует цепочку вывода!
     */
    kolibri_ls_add_equals(&ls, 0, 2, 0, "Достали яблоко из коробки 3 (надпись 'Я+Г') → там Яблоки");

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("Результат: %s\n", ret == 0 ? "РЕШЕНО" : (ret == 1 ? "ЧАСТИЧНО" : "ПРОТИВОРЕЧИЕ"));
    printf("\nОтвет:\n%s\n", sol.answer);

    /* Проверка: должно быть решено */
    printf("Решено: %s\n", sol.solved ? "ДА ✓" : "НЕТ ✗");
    
    /* Проверяем что все значения определены */
    for (int e = 0; e < 3; e++) {
        printf("  Коробка %d (%s) → %s\n", e + 1,
               e == 0 ? "надпись 'Яблоки'" : (e == 1 ? "надпись 'Груши'" : "надпись 'Я+Г'"),
               ls.values[0][e] >= 0 ? contents[ls.values[0][e]] : "???");
    }

    kolibri_ls_print_solution(&sol);

    /* Должно быть решено */
    assert(sol.solved);
    assert(ls.values[0][2] == 0);  /* Коробка 3 = Яблоки */
    /* Коробка 1 ≠ Яблоки и ≠ Груши (потому что Груши уже у коробки 2) → Я+Г */
    /* Коробка 2 ≠ Груши и ≠ Я+Г → Груши */

    printf("✓ Тест коробок завершён\n\n");
}

/* ============================================================================
 * ТЕСТ: 8 монет, 1 фальшивая (легче), 2 взвешивания
 * ============================================================================ */

void test_8_coins_logical(void) {
    printf("=== Тест: 8 монет, фальшивая легче, 2 взвешивания ===\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    /* Домен: монеты 1..8, значение = фальшивая или нет */
    const char *coins[] = {
        "Монета 1", "Монета 2", "Монета 3", "Монета 4",
        "Монета 5", "Монета 6", "Монета 7", "Монета 8"
    };
    
    /* Домен "какая фальшивая": 8 possibilities */
    const char *fake[] = {"Фальшивая"};
    kolibri_ls_add_domain(&ls, "фальшивая", coins, 8);

    /* Ограничение: ровно 1 фальшивая */
    kolibri_ls_add_all_different(&ls, 0, "Ровно 1 фальшивая");

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("Результат: %s\n", ret == 0 ? "РЕШЕНО" : (ret == 1 ? "ЧАСТИЧНО" : "ПРОТИВОРЕЧИЕ"));
    printf("\nОтвет:\n%s\n", sol.answer);
    
    kolibri_ls_print_solution(&sol);

    printf("✓ Тест монет завершён\n\n");
}

/* ============================================================================
 * ТЕСТ: Эйнштейновская загадка (упрощённая)
 * ============================================================================ */

void test_einstein_puzzle(void) {
    printf("=== Тест: Упрощённая загадка Эйнштейна ===\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    /* 3 дома, 3 цвета, 3 национальности, 3 напитка */
    const char *colors[] = {"Красный", "Зелёный", "Синий"};
    const char *nations[] = {"Англичанин", "Испанец", "Японец"};
    const char *drinks[] = {"Чай", "Кофе", "Молоко"};

    kolibri_ls_add_domain(&ls, "цвет дома", colors, 3);
    kolibri_ls_add_domain(&ls, "национальность", nations, 3);
    kolibri_ls_add_domain(&ls, "напиток", drinks, 3);

    /* Ограничения */
    kolibri_ls_add_all_different(&ls, 0, "Все дома разные цвета");
    kolibri_ls_add_all_different(&ls, 1, "Все национальности разные");
    kolibri_ls_add_all_different(&ls, 2, "Все напитки разные");

    /* Англичанин живёт в красном доме: национальность[0] → цвет[0] */
    /* (Упрощаем: entity 0 в каждом домене связан) */
    
    /* Испанец пьёт чай */
    /* Молоко пьёт в среднем доме */

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("Результат: %s\n", ret == 0 ? "РЕШЕНО" : (ret == 1 ? "ЧАСТИЧНО" : "ПРОТИВОРЕЧИЕ"));
    printf("\nОтвет:\n%s\n", sol.answer);

    kolibri_ls_print_grid(&ls);
    kolibri_ls_print_solution(&sol);

    printf("✓ Тест Эйнштейна завершён\n\n");
}

/* ============================================================================
 * ТЕСТ: Противоречие
 * ============================================================================ */

void test_contradiction(void) {
    printf("=== Тест: Обнаружение противоречия ===\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    const char *vals[] = {"A", "B", "C"};
    kolibri_ls_add_domain(&ls, "test", vals, 3);

    /* Противоречивые ограничения */
    kolibri_ls_add_equals(&ls, 0, 0, 0, "Entity 0 = A");
    kolibri_ls_add_equals(&ls, 0, 0, 1, "Entity 0 = B");  /* Противоречие! */

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("Результат: %s\n", ret == 0 ? "РЕШЕНО" : (ret == 1 ? "ЧАСТИЧНО" : "ПРОТИВОРЕЧИЕ"));
    
    /* Должно обнаружить противоречие или частичное решение */
    assert(ret != 0);  /* Не должно быть решено */

    printf("✓ Тест противоречия завершён\n\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    printf("===========================================\n");
    printf("Kolibri Logical Solver Tests\n");
    printf("===========================================\n\n");

    test_light_switches();
    test_mislabeled_boxes();
    test_8_coins_logical();
    test_einstein_puzzle();
    test_contradiction();

    printf("===========================================\n");
    printf("All tests completed! ✓\n");
    printf("===========================================\n");

    return 0;
}
