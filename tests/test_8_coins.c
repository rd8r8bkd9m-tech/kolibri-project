/*
 * test_8_coins.c
 *
 * Тест: Kolibri решает задачу про 8 монет
 */

#include "kolibri/reasoning_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *problem =
        "У тебя есть 8 монет, и одна из них фальшивая, причём она легче остальных. "
        "Есть весы с двумя чашами, но без гирь. "
        "Нужно за 2 взвешивания точно определить фальшивую монету. "
        "Решение: раздели 8 монет на группы 3+3+2. "
        "Первое взвешивание: 3 против 3. "
        "Если равны - фальшивая в оставшихся 2, второе: 1 против 1. "
        "Если не равны - фальшивая в лёгкой тройке, второе: 1 против 1 из трёх.";

    printf("===========================================\n");
    printf("Kolibri: Задача про 8 монет\n");
    printf("===========================================\n\n");
    printf("Задача:\n%s\n\n", problem);

    KolibriREConfig config = {0};
    kolibri_re_init(&config);

    KolibriReasoningResult result;
    int ret = kolibri_re_solve_logic_puzzle(problem, &config, &result);

    if (ret != 0) {
        printf("ERROR: reasoning failed (ret=%d)\n", ret);
        return 1;
    }

    printf("=== Reasoning Result ===\n");
    printf("Query: %s\n", result.query);
    printf("Type: %s\n", kolibri_re_type_name(result.primary_type));
    printf("Answer: %s\n\n", result.answer);
    
    printf("Reasoning Chain (%d steps):\n", result.chain.num_steps);
    for (int i = 0; i < result.chain.num_steps; i++) {
        const KolibriReasoningStep *s = &result.chain.steps[i];
        printf("  Step %d [%s]: %s\n",
               s->step_num, kolibri_re_type_name(s->type), s->description);
        printf("    Premise: %s\n", s->premise);
        printf("    Conclusion: %s\n", s->conclusion);
        printf("    Confidence: %.2f\n", s->confidence);
    }

    printf("\nConfidence: %.2f (%s)\n",
           result.confidence, result.confidence_reason);
    printf("Time: %.1fms\n", result.reasoning_time_ms);
    printf("=========================\n\n");

    printf("===========================================\n");
    printf("Ответ Kolibri:\n%s\n", result.answer);
    printf("===========================================\n");
    printf("Ход рассуждений:\n");
    for (int i = 0; i < result.chain.num_steps; i++) {
        printf("  Шаг %d: %s\n",
               result.chain.steps[i].step_num,
               result.chain.steps[i].description);
    }
    printf("\nУверенность: %.0f%%\n", result.confidence * 100);

    return 0;
}
