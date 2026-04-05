/*
 * kolibri_solve
 *
 * Консольное приложение для решения логических задач через constraint solver
 *
 * Использование:
 *   ./kolibri_solve --boxes       Задача про 3 коробки
 *   ./kolibri_solve --coins N W   N монет, W взвешиваний
 *   ./kolibri_solve --text "текст"  Решить задачу из текста
 *   ./kolibri_solve --stdin       Читать задачу из stdin
 *   ./kolibri_solve --interactive Интерактивный режим
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/logical_solver.h"
#include "kolibri/fact_extractor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void solve_boxes(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Задача: 3 коробки с неправильными надписями\n");
    printf("═══════════════════════════════════════════════════\n\n");

    printf("Условие:\n");
    printf("  Коробка 1: надпись 'Только яблоки'\n");
    printf("  Коробка 2: надпись 'Только груши'\n");
    printf("  Коробка 3: надпись 'Яблоки и груши'\n");
    printf("  Все надписи НЕПРАВИЛЬНЫЕ.\n");
    printf("  Действие: достаём фрукт из коробки 3 → яблоко\n\n");

    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    const char *contents[] = {"Яблоки", "Груши", "Яблоки+Груши"};
    kolibri_ls_add_domain(&ls, "коробка→содержимое", contents, 3);

    /* Все надписи неправильные */
    kolibri_ls_add_not(&ls, 0, 0, 0, "Надпись 'Яблоки' неправильная");
    kolibri_ls_add_not(&ls, 0, 1, 1, "Надпись 'Груши' неправильная");
    kolibri_ls_add_not(&ls, 0, 2, 2, "Надпись 'Я+Г' неправильная");

    /* Все коробки разные */
    kolibri_ls_add_all_different(&ls, 0, "Все коробки разные");

    /* Ключевое наблюдение */
    kolibri_ls_add_equals(&ls, 0, 2, 0, "Достали яблоко из коробки 3 (надпись 'Я+Г') → там Яблоки");

    KolibriLSSolution sol;
    int ret = kolibri_ls_solve(&ls, &sol);

    printf("─── Ход рассуждений ───────────────────────────────\n\n");
    for (int i = 0; i < sol.steps_count; i++) {
        const KolibriLSStep *s = &sol.steps[i];
        printf("  Шаг %d. %s\n", s->step_num, s->description);
        if (strlen(s->detail) > 0 && strcmp(s->detail, s->description) != 0) {
            printf("    → %s\n", s->detail);
        }
        printf("    [уверенность: %.0f%%]\n\n", s->confidence * 100);
    }

    printf("─── Ответ ─────────────────────────────────────────\n\n");
    printf("Коробка 1 (надпись 'Яблоки') → %s\n",
           ls.values[0][0] >= 0 ? contents[ls.values[0][0]] : "???");
    printf("Коробка 2 (надпись 'Груши') → %s\n",
           ls.values[0][1] >= 0 ? contents[ls.values[0][1]] : "???");
    printf("Коробка 3 (надпись 'Я+Г') → %s\n",
           ls.values[0][2] >= 0 ? contents[ls.values[0][2]] : "???");

    printf("\n─── Итого ─────────────────────────────────────────\n");
    printf("  Статус: %s\n", sol.solved ? "РЕШЕНО ✓" : "НЕ РЕШЕНО ✗");
    printf("  Шагов вывода: %d\n", sol.steps_count);
    printf("  Уверенность: %.0f%%\n", sol.confidence * 100);
    printf("  Время: %.1fms\n", ls.solve_time_ms);
    printf("═══════════════════════════════════════════════════\n");
}

static void solve_coins(int n, int w) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Задача: %d монет, фальшивая легче, %d взвешиваний\n", n, w);
    printf("═══════════════════════════════════════════════════\n\n");

    /* Проверяем решаемо ли */
    int max_detectable = 1;
    for (int i = 0; i < w; i++) max_detectable *= 3;

    printf("Проверка: 3^%d = %d, монет = %d\n", w, max_detectable, n);

    if (max_detectable < n) {
        printf("  ❌ НЕ решаемо: нужно минимум %d взвешиваний\n\n",
               (int)ceil(log(n) / log(3)));
        return;
    }

    printf("  ✓ Решаемо!\n\n");

    /* Показываем алгоритм */
    int group = (n + 2) / 3;
    int g1 = group, g2 = group, g3 = n - g1 - g2;

    printf("Алгоритм:\n");
    printf("1. Раздели %d монет: %d + %d + %d\n", n, g1, g2, g3);
    printf("2. Взвесь %d vs %d\n", g1, g2);
    printf("   - Равны → фальшивая среди %d отложенных\n", g3);
    printf("   - Не равны → фальшивая в лёгкой чаше (%d монет)\n", g1);
    printf("3. Повторяй с подозрительной группой\n\n");

    /* Решаем через solver */
    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    char domain_name[64];
    snprintf(domain_name, sizeof(domain_name), "монета 1..%d", n);
    
    const char *coins[16];
    char coin_names[16][16];
    for (int i = 0; i < n && i < 16; i++) {
        snprintf(coin_names[i], sizeof(coin_names[i]), "М%d", i + 1);
        coins[i] = coin_names[i];
    }
    
    kolibri_ls_add_domain(&ls, domain_name, coins, n);
    kolibri_ls_add_all_different(&ls, 0, "Ровно 1 фальшивая");

    KolibriLSSolution sol;
    kolibri_ls_solve(&ls, &sol);

    printf("─── Итого ─────────────────────────────────────────\n");
    printf("  Статус: %s\n", sol.solved ? "РЕШЕНО ✓" : "ЧАСТИЧНО");
    printf("  Шагов вывода: %d\n", sol.steps_count);
    printf("═══════════════════════════════════════════════════\n");
}

static void solve_text(const char *text) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Kolibri Logical Solver — Автоматическое решение\n");
    printf("═══════════════════════════════════════════════════\n\n");

    printf("Задача:\n%s\n\n", text);

    /* Извлекаем факты */
    KolibriFEExtractedData data;
    kolibri_fe_extract(text, &data);

    printf("─── Извлечённые факты ───────────────────────────\n");
    kolibri_fe_print_extracted(&data);
    printf("\n");

    /* Решаем */
    KolibriLogicalSolver ls;
    kolibri_ls_init(&ls);

    KolibriLSSolution solution;
    int ret = kolibri_fe_solve(&data, &ls, &solution);

    printf("─── Решение ─────────────────────────────────────\n");
    printf("  Тип: %s\n", data.task_name);
    printf("  Статус: %s\n", solution.solved ? "РЕШЕНО ✓" : (ret == -1 ? "ПРОТИВОРЕЧИЕ" : "ЧАСТИЧНО"));
    printf("  Шагов вывода: %d\n", solution.steps_count);
    printf("  Уверенность: %.0f%%\n", solution.confidence * 100);
    printf("  Время: %.1fms\n", ls.solve_time_ms);

    printf("\n─── Ответ ─────────────────────────────────────\n\n");
    printf("%s\n", solution.answer);

    printf("\n─── Ход рассуждений ───────────────────────────\n\n");
    for (int i = 0; i < solution.steps_count; i++) {
        const KolibriLSStep *s = &solution.steps[i];
        printf("  Шаг %d. %s\n", s->step_num, s->description);
        if (strlen(s->detail) > 0 && strcmp(s->detail, s->description) != 0) {
            printf("    → %s\n", s->detail);
        }
    }

    printf("\n═══════════════════════════════════════════════════\n");
}

static void print_usage(const char *prog) {
    printf("Kolibri Logical Solver — решение логических задач\n\n");
    printf("Использование:\n");
    printf("  %s --boxes          Задача про 3 коробки\n", prog);
    printf("  %s --coins N W      N монет, W взвешиваний\n", prog);
    printf("  %s --text \"задача\"  Решить задачу из текста\n", prog);
    printf("  %s --stdin          Читать задачу из stdin\n", prog);
    printf("  %s --help           Помощь\n", prog);
    printf("\nПримеры:\n");
    printf("  %s --text \"В комнате 3 выключателя...\"\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--boxes") == 0 || strcmp(argv[1], "-b") == 0) {
        solve_boxes();
        return 0;
    }

    if (strcmp(argv[1], "--coins") == 0 || strcmp(argv[1], "-c") == 0) {
        int n = 8, w = 2;
        if (argc >= 4) {
            n = atoi(argv[2]);
            w = atoi(argv[3]);
        }
        solve_coins(n, w);
        return 0;
    }

    if (strcmp(argv[1], "--text") == 0 || strcmp(argv[1], "-t") == 0) {
        if (argc < 3) {
            printf("Ошибка: нужен текст задачи\n");
            return 1;
        }
        /* Собираем все аргументы после --text */
        char text[8192] = {0};
        size_t offset = 0;
        for (int i = 2; i < argc; i++) {
            size_t len = strlen(argv[i]);
            if (offset + len + 1 < sizeof(text)) {
                if (i > 2) text[offset++] = ' ';
                strncpy(text + offset, argv[i], sizeof(text) - offset - 1);
                offset += len;
            }
        }
        text[offset] = '\0';
        solve_text(text);
        return 0;
    }

    if (strcmp(argv[1], "--stdin") == 0 || strcmp(argv[1], "-s") == 0) {
        char text[8192] = {0};
        printf("Введи задачу (Ctrl+D для завершения):\n\n> ");
        size_t total = 0;
        while (fgets(text + total, sizeof(text) - total, stdin)) {
            total = strlen(text);
            if (total > 0 && text[total - 1] == '\n') {
                text[total - 1] = '\0';
                break;
            }
        }
        if (strlen(text) == 0) {
            printf("Пустой ввод.\n");
            return 1;
        }
        solve_text(text);
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
