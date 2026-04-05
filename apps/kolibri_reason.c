/*
 * kolibri_reason
 *
 * Консольное приложение для решения логических задач через reasoning engine
 *
 * Использование:
 *   ./kolibri_reason "задача"
 *   ./kolibri_reason --stdin  (читает из stdin)
 *
 * Copyright (c) 2025 Кочуров Владислав Евгеньевич
 */

#include "kolibri/reasoning_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *prog) {
    printf("Kolibri Reasoning Engine — решение логических задач\n\n");
    printf("Использование:\n");
    printf("  %s \"текст задачи\"\n", prog);
    printf("  %s --stdin\n", prog);
    printf("\nПримеры:\n");
    printf("  %s \"У тебя 8 монет, одна фальшивая (легче). Как за 2 взвешивания найти?\"\n", prog);
    printf("  %s --stdin\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    char problem[4096] = {0};

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "--stdin") == 0) {
        printf("Введи задачу (Ctrl+D для завершения):\n\n> ");
        size_t total = 0;
        while (fgets(problem + total, sizeof(problem) - total, stdin)) {
            total = strlen(problem);
            if (total > 0 && problem[total - 1] == '\n') {
                problem[total - 1] = '\0';
                break;
            }
        }
        if (strlen(problem) == 0) {
            printf("Пустой ввод.\n");
            return 1;
        }
    } else {
        /* Собираем все аргументы */
        size_t offset = 0;
        for (int i = 1; i < argc; i++) {
            size_t len = strlen(argv[i]);
            if (offset + len + 1 < sizeof(problem)) {
                if (i > 1) problem[offset++] = ' ';
                strncpy(problem + offset, argv[i], sizeof(problem) - offset - 1);
                offset += len;
            }
        }
        problem[offset] = '\0';
    }

    printf("═══════════════════════════════════════════════════\n");
    printf("  Kolibri Reasoning Engine v1.0\n");
    printf("═══════════════════════════════════════════════════\n\n");
    printf("Задача:\n%s\n\n", problem);

    KolibriREConfig config = {0};
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_solve_logic_puzzle(problem, &config, &result);

    if (ret != 0) {
        printf("ERROR: reasoning failed (ret=%d)\n", ret);
        return 1;
    }

    printf("─── Ход рассуждений ───────────────────────────────\n\n");

    for (int i = 0; i < result.chain.num_steps; i++) {
        const KolibriReasoningStep *s = &result.chain.steps[i];
        printf("  Шаг %d. %s\n", s->step_num, s->description);
        if (strlen(s->premise) > 0 && strcmp(s->premise, s->description) != 0) {
            printf("    → %s\n", s->premise);
        }
        if (strlen(s->conclusion) > 0 && strcmp(s->conclusion, s->premise) != 0) {
            printf("    = %s\n", s->conclusion);
        }
        printf("    [уверенность: %.0f%%]\n\n", s->confidence * 100);
    }

    printf("─── Ответ ─────────────────────────────────────────\n\n");
    printf("%s\n\n", result.answer);

    printf("─── Итого ─────────────────────────────────────────\n");
    printf("  Тип рассуждения: %s\n", kolibri_re_type_name(result.primary_type));
    printf("  Шагов: %d\n", result.chain.num_steps);
    printf("  Уверенность: %.0f%%\n", result.confidence * 100);
    printf("  Время: %.1fms\n", result.reasoning_time_ms);
    printf("═══════════════════════════════════════════════════\n");

    return 0;
}
